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
 * system_catalog.c - Catalog manager
 */

#ident "$Id$"

#include <stdlib.h>
#include <string.h>

#include <time.h>

#include "error_manager.h"
#include "system_catalog.h"
#include "log_manager.h"
#include "memory_hash.h"
#include "page_buffer.h"
#include "file_manager.h"
#include "file_io.h"
#include "slotted_page.h"
#include "oid.h"
#include "extendible_hash.h"
#include "memory_alloc.h"
#include "object_representation_sr.h"
#include "object_representation.h"
#include "boot_sr.h"
#include "btree_load.h"
#include "heap_file.h"
#include "storage_common.h"
#include "xserver_interface.h"
#include "statistics_sr.h"
#include "partition.h"
#include "lock_free.h"

#if defined(SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_trylock(a)   0
#define pthread_mutex_unlock(a)
static int rv;
#endif /* not SERVER_MODE */

#define CATALOG_HEADER_SLOT             0
#define CATALOG_MAX_SLOT_ID_SIZE        12
#define CATALOG_HASH_SIZE               1000
#define CATALOG_KEY_VALUE_ARRAY_SIZE    1000

#define CATALOG_PGHEADER_OVFL_PGID_PAGEID_OFF  0
#define CATALOG_PGHEADER_OVFL_PGID_VOLID_OFF   4
#define CATALOG_PGHEADER_DIR_CNT_OFF           8
#define CATALOG_PGHEADER_PG_OVFL_OFF           12

#define CATALOG_PAGE_HEADER_SIZE                  16

/* READERS for CATALOG_PAGE_HEADER related fields */
#define CATALOG_GET_PGHEADER_OVFL_PGID_PAGEID(ptr) \
   (PAGEID) OR_GET_INT ((ptr) + CATALOG_PGHEADER_OVFL_PGID_PAGEID_OFF)

#define CATALOG_GET_PGHEADER_OVFL_PGID_VOLID(ptr) \
  (VOLID) OR_GET_SHORT ((ptr) + CATALOG_PGHEADER_OVFL_PGID_VOLID_OFF)

#define CATALOG_GET_PGHEADER_DIR_COUNT(ptr) \
   (int) OR_GET_INT ((ptr) + CATALOG_PGHEADER_DIR_CNT_OFF)

#define CATALOG_GET_PGHEADER_PG_OVFL(ptr) \
   (bool) OR_GET_INT ((ptr) + CATALOG_PGHEADER_PG_OVFL_OFF)

/* WRITERS for CATALOG_PAGE_HEADER related fields */
#define CATALOG_PUT_PGHEADER_OVFL_PGID_PAGEID(ptr,val) \
  OR_PUT_INT ((ptr) + CATALOG_PGHEADER_OVFL_PGID_PAGEID_OFF, (val))

#define CATALOG_PUT_PGHEADER_OVFL_PGID_VOLID(ptr,val) \
  OR_PUT_SHORT ((ptr) + CATALOG_PGHEADER_OVFL_PGID_VOLID_OFF, (val))

#define CATALOG_PUT_PGHEADER_DIR_COUNT(ptr,val) \
  OR_PUT_INT ((ptr) + CATALOG_PGHEADER_DIR_CNT_OFF, (val))

#define CATALOG_PUT_PGHEADER_PG_OVFL(ptr,val) \
  OR_PUT_INT ((ptr) + CATALOG_PGHEADER_PG_OVFL_OFF, (int) (val))

/* Each disk representation is aligned with MAX_ALIGNMENT */
#define CATALOG_DISK_REPR_ID_OFF             0
#define CATALOG_DISK_REPR_N_FIXED_OFF        4
#define CATALOG_DISK_REPR_FIXED_LENGTH_OFF   8
#define CATALOG_DISK_REPR_N_VARIABLE_OFF     12
#define CATALOG_DISK_REPR_RESERVED_1_OFF     16	/* reserved for future use */
#define CATALOG_DISK_REPR_SIZE               56

/* Each disk attribute is aligned with MAX_ALIGNMENT
   Each disk attribute may be followed by a "value" which is of
   variable size. The below constants does not consider the
   optional value field following the attribute structure. */
#define CATALOG_DISK_ATTR_ID_OFF         0
#define CATALOG_DISK_ATTR_LOCATION_OFF   4
#define CATALOG_DISK_ATTR_TYPE_OFF       8
#define CATALOG_DISK_ATTR_VAL_LENGTH_OFF 12
#define CATALOG_DISK_ATTR_POSITION_OFF   16
#define CATALOG_DISK_ATTR_CLASSOID_OFF   20
#define CATALOG_DISK_ATTR_N_BTSTATS_OFF  28
#define CATALOG_DISK_ATTR_SIZE           80

#define CATALOG_BT_STATS_BTID_OFF        0
#define CATALOG_BT_STATS_LEAFS_OFF       OR_BTID_ALIGNED_SIZE
#define CATALOG_BT_STATS_PAGES_OFF       16
#define CATALOG_BT_STATS_HEIGHT_OFF      20
#define CATALOG_BT_STATS_KEYS_OFF        24
#define CATALOG_BT_STATS_FUNC_INDEX_OFF	 28
#define CATALOG_BT_STATS_PKEYS_OFF       32
#define CATALOG_BT_STATS_RESERVED_OFF    (CATALOG_BT_STATS_PKEYS_OFF + (OR_INT_SIZE * BTREE_STATS_PKEYS_NUM))	/* 64 */
#define CATALOG_BT_STATS_SIZE            (CATALOG_BT_STATS_RESERVED_OFF + (OR_INT_SIZE * BTREE_STATS_RESERVED_NUM))	/* 64 + (4 * R_NUM) = 80 */

#define CATALOG_GET_BT_STATS_BTID(var, ptr) \
    OR_GET_BTID((ptr) + CATALOG_BT_STATS_BTID_OFF, (var))

#define CATALOG_CLS_INFO_HFID_OFF           0
#define CATALOG_CLS_INFO_TOT_PAGES_OFF     12
#define CATALOG_CLS_INFO_TOT_OBJS_OFF      16
#define CATALOG_CLS_INFO_TIME_STAMP_OFF    20
#define CATALOG_CLS_INFO_REP_DIR_OFF       24
#define CATALOG_CLS_INFO_SIZE              56
#define CATALOG_CLS_INFO_RESERVED          24

#define CATALOG_REPR_ITEM_PAGEID_PAGEID_OFF   0
#define CATALOG_REPR_ITEM_PAGEID_VOLID_OFF    4
#define CATALOG_REPR_ITEM_REPRID_OFF          8
#define CATALOG_REPR_ITEM_SLOTID_OFF         10
#define CATALOG_REPR_ITEM_COUNT_OFF          12

#define CATALOG_REPR_ITEM_SIZE               16

#define CATALOG_GET_REPR_ITEM_PAGEID_PAGEID(ptr) \
  (PAGEID) OR_GET_INT ((ptr) + CATALOG_REPR_ITEM_PAGEID_PAGEID_OFF)

#define CATALOG_GET_REPR_ITEM_PAGEID_VOLID(ptr) \
  (VOLID) OR_GET_SHORT ((ptr) + CATALOG_REPR_ITEM_PAGEID_VOLID_OFF)

#define CATALOG_GET_REPR_ITEM_REPRID(ptr) \
  (REPR_ID) OR_GET_SHORT ((ptr) + CATALOG_REPR_ITEM_REPRID_OFF)

#define CATALOG_GET_REPR_ITEM_SLOTID(ptr) \
  (PGSLOTID) OR_GET_SHORT ((ptr) + CATALOG_REPR_ITEM_SLOTID_OFF)

#define CATALOG_GET_REPR_ITEM_COUNT(ptr) \
  (PGSLOTID) OR_GET_BYTE ((ptr) + CATALOG_REPR_ITEM_COUNT_OFF)

/* catalog estimated max. space information */
typedef struct catalog_max_space CATALOG_MAX_SPACE;
struct catalog_max_space
{
  VPID max_page_id;		/* estimated maximum space page identifier */
  PGLENGTH max_space;		/* estimated maximum space */
};

/* catalog key for the hash table */
typedef struct catalog_key CATALOG_KEY;
struct catalog_key
{
  /* actual key */
  PAGEID page_id;
  VOLID volid;
  PGSLOTID slot_id;
  REPR_ID repr_id;

  /* these are part of the data, but we want them inserted atomically */
  VPID r_page_id;		/* location of representation */
  PGSLOTID r_slot_id;
};				/* class identifier + representation identifier */

/* catalog value for the hash table */
typedef struct catalog_entry CATALOG_ENTRY;
struct catalog_entry
{
  CATALOG_ENTRY *stack;		/* used for freelist */
  CATALOG_ENTRY *next;		/* next in hash chain */
  UINT64 del_id;		/* delete transaction ID (for lock free) */
  CATALOG_KEY key;		/* key of catalog entry */
};

/* handling functions for catalog key and entry */
static void *catalog_entry_alloc (void);
static int catalog_entry_free (void *ent);
static int catalog_entry_init (void *ent);
static int catalog_entry_uninit (void *ent);
static int catalog_key_copy (void *src, void *dest);
static int catalog_key_compare (void *key1, void *key2);
static unsigned int catalog_key_hash (void *key, int htsize);

/* catalog entry descriptor */
static LF_ENTRY_DESCRIPTOR catalog_entry_Descriptor = {
  /* offsets */
  offsetof (CATALOG_ENTRY, stack),
  offsetof (CATALOG_ENTRY, next),
  offsetof (CATALOG_ENTRY, del_id),
  offsetof (CATALOG_ENTRY, key),
  0,

  /* using mutex? */
  LF_EM_NOT_USING_MUTEX,

  catalog_entry_alloc,
  catalog_entry_free,
  catalog_entry_init,
  catalog_entry_uninit,
  catalog_key_copy,
  catalog_key_compare,
  catalog_key_hash,
  NULL				/* no inserts */
};

typedef struct catalog_class_id_list CATALOG_CLASS_ID_LIST;
struct catalog_class_id_list
{
  OID class_id;
  CATALOG_CLASS_ID_LIST *next;
};

typedef struct catalog_record CATALOG_RECORD;
struct catalog_record
{
  VPID vpid;			/* (volume id, page id) of the slotted page record */
  PGSLOTID slotid;		/* slot id of the slotted page record record */
  PAGE_PTR page_p;		/* pointer to the page fetched */
  RECDES recdes;		/* record descriptor to be fetched and copied */
  int offset;			/* offset in the record data */
};

typedef struct catalog_page_header CATALOG_PAGE_HEADER;
struct catalog_page_header
{
  VPID overflow_page_id;	/* overflow page identifier */
  int dir_count;		/* number of directories in page */
  bool is_overflow_page;	/* true if page is overflow page, false or else */
};

typedef struct catalog_repr_item CATALOG_REPR_ITEM;
struct catalog_repr_item
{
  VPID page_id;			/* page identifier of the representation */
  INT16 repr_id;		/* representation identifier */
  PGSLOTID slot_id;		/* page slot identifier of representation */
};

#define CATALOG_REPR_ITEM_INITIALIZER \
  { { NULL_PAGEID, NULL_VOLID }, NULL_REPRID, NULL_SLOTID }

CTID catalog_Id;		/* global catalog identifier */
static PGLENGTH catalog_Max_record_size;	/* Maximum Record Size */

/*
 * Note: Catalog memory hash table operations are NOT done in CRITICAL
 * SECTIONS, because there can not be simultaneous updaters and readers
 * for the same class representation information.
 */
static LF_FREELIST catalog_Hash_freelist = LF_FREELIST_INITIALIZER;
static LF_HASH_TABLE catalog_Hash_table = LF_HASH_TABLE_INITIALIZER;

static CATALOG_MAX_SPACE catalog_Max_space;	/* Global space information */
static pthread_mutex_t catalog_Max_space_lock = PTHREAD_MUTEX_INITIALIZER;
static bool catalog_is_header_initialized = false;

typedef struct catalog_find_optimal_page_context CATALOG_FIND_OPTIMAL_PAGE_CONTEXT;
struct catalog_find_optimal_page_context
{
  int size;
  int size_optimal_free;
  VPID vpid_optimal;
  PAGE_PTR page_optimal;
};

#if defined (SA_MODE)
typedef struct catalog_page_collector CATALOG_PAGE_COLLECTOR;
struct catalog_page_collector
{
  VPID *vpids;
  int n_vpids;
};
#endif /* SA_MODE */

typedef struct catalog_page_dump_context CATALOG_PAGE_DUMP_CONTEXT;
struct catalog_page_dump_context
{
  FILE *fp;
  int page_index;
};

static void catalog_initialize_max_space (CATALOG_MAX_SPACE * header_p);
static void catalog_update_max_space (VPID * page_id, PGLENGTH space);

static int catalog_initialize_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page, void *args);
static PAGE_PTR catalog_get_new_page (THREAD_ENTRY * thread_p, VPID * page_id, bool is_overflow_page);
static PAGE_PTR catalog_find_optimal_page (THREAD_ENTRY * thread_p, int size, VPID * page_id);
static int catalog_get_key_list (THREAD_ENTRY * thread_p, void *key, void *val, void *args);
static void catalog_free_key_list (CATALOG_CLASS_ID_LIST * clsid_list);
static int catalog_put_record_into_page (THREAD_ENTRY * thread_p, CATALOG_RECORD * ct_recordp, int next,
					 PGSLOTID * remembered_slotid);
static int catalog_store_disk_representation (THREAD_ENTRY * thread_p, DISK_REPR * disk_reprp,
					      CATALOG_RECORD * ct_recordp, PGSLOTID * remembered_slotid);
static int catalog_store_disk_attribute (THREAD_ENTRY * thread_p, DISK_ATTR * disk_attrp, CATALOG_RECORD * ct_recordp,
					 PGSLOTID * remembered_slotid);
static int catalog_store_attribute_value (THREAD_ENTRY * thread_p, void *value, int length, CATALOG_RECORD * ct_recordp,
					  PGSLOTID * remembered_slotid);
static int catalog_store_btree_statistics (THREAD_ENTRY * thread_p, BTREE_STATS * bt_statsp,
					   CATALOG_RECORD * ct_recordp, PGSLOTID * remembered_slotid);
static int catalog_get_record_from_page (THREAD_ENTRY * thread_p, CATALOG_RECORD * ct_recordp);
static int catalog_fetch_disk_representation (THREAD_ENTRY * thread_p, DISK_REPR * disk_reprp,
					      CATALOG_RECORD * ct_recordp);
static int catalog_fetch_disk_attribute (THREAD_ENTRY * thread_p, DISK_ATTR * disk_attrp, CATALOG_RECORD * ct_recordp);
static int catalog_fetch_attribute_value (THREAD_ENTRY * thread_p, void *value, int length,
					  CATALOG_RECORD * ct_recordp);
static int catalog_fetch_btree_statistics (THREAD_ENTRY * thread_p, BTREE_STATS * bt_statsp,
					   CATALOG_RECORD * ct_recordp);
static int catalog_drop_disk_representation_from_page (THREAD_ENTRY * thread_p, VPID * page_id, PGSLOTID slot_id);
static int catalog_drop_representation_class_from_page (THREAD_ENTRY * thread_p, VPID * dir_pgid, PAGE_PTR * dir_pgptr,
							VPID * page_id, PGSLOTID slot_id);
static int catalog_get_rep_dir (THREAD_ENTRY * thread_p, OID * class_oid_p, OID * rep_dir_p, bool lookup_hash);
static PAGE_PTR catalog_get_representation_record (THREAD_ENTRY * thread_p, OID * rep_dir_p, RECDES * record_p,
						   PGBUF_LATCH_MODE latch, int is_peek, int *out_repr_count_p);
static PAGE_PTR catalog_get_representation_record_after_search (THREAD_ENTRY * thread_p, OID * class_id_p,
								RECDES * record_p, PGBUF_LATCH_MODE latch, int is_peek,
								OID * rep_dir_p, int *out_repr_count_p,
								bool lookup_hash);
static int catalog_adjust_directory_count (THREAD_ENTRY * thread_p, PAGE_PTR page_p, RECDES * record_p, int delta);
static void catalog_delete_key (OID * class_id_p, REPR_ID repr_id);
static char *catalog_find_representation_item_position (INT16 repr_id, int repr_cnt, char *repr_p, int *out_position_p);
static int catalog_insert_representation_item (THREAD_ENTRY * thread_p, RECDES * record_p, OID * rep_dir_p);
static int catalog_drop_directory (THREAD_ENTRY * thread_p, PAGE_PTR page_p, RECDES * record_p, OID * oid_p,
				   OID * class_id_p);
static void catalog_copy_btree_statistic (BTREE_STATS * new_btree_stats_p, int new_btree_stats_count,
					  BTREE_STATS * pre_btree_stats_p, int pre_btree_stats_count);
static void catalog_copy_disk_attributes (DISK_ATTR * new_attrs_p, int new_attr_count, DISK_ATTR * pre_attrs_p,
					  int pre_attr_count);
static int catalog_sum_disk_attribute_size (DISK_ATTR * attrs_p, int count);

static int catalog_put_representation_item (THREAD_ENTRY * thread_p, OID * class_id, CATALOG_REPR_ITEM * repr_item,
					    OID * rep_dir_p);
static int catalog_get_representation_item (THREAD_ENTRY * thread_p, OID * class_id, CATALOG_REPR_ITEM * repr_item);
static int catalog_drop_representation_item (THREAD_ENTRY * thread_p, OID * class_id, CATALOG_REPR_ITEM * repr_item);
static int catalog_drop (THREAD_ENTRY * thread_p, OID * class_id, REPR_ID repr_id);
static int catalog_drop_all (THREAD_ENTRY * thread_p, OID * class_id);
static int catalog_drop_all_representation_and_class (THREAD_ENTRY * thread_p, OID * class_id);
static int catalog_fixup_missing_disk_representation (THREAD_ENTRY * thread_p, OID * class_oid, REPR_ID reprid);
static int catalog_fixup_missing_class_info (THREAD_ENTRY * thread_p, OID * class_oid);
static DISK_ISVALID catalog_check_class_consistency (THREAD_ENTRY * thread_p, OID * class_oid);
static void catalog_dump_disk_attribute (DISK_ATTR * atr);
static void catalog_dump_representation (DISK_REPR * dr);
static void catalog_clear_hash_table (void);

static void catalog_put_page_header (char *rec_p, CATALOG_PAGE_HEADER * header_p);
static void catalog_get_disk_representation (DISK_REPR * disk_repr_p, char *rec_p);
static void catalog_put_disk_representation (char *rec_p, DISK_REPR * disk_repr_p);
static void catalog_get_disk_attribute (DISK_ATTR * attr_p, char *rec_p);
static void catalog_put_disk_attribute (char *rec_p, DISK_ATTR * attr_p);
static void catalog_put_btree_statistics (char *rec_p, BTREE_STATS * stat_p);
static void catalog_get_class_info_from_record (CLS_INFO * class_info_p, char *rec_p);
static void catalog_put_class_info_to_record (char *rec_p, CLS_INFO * class_info_p);
static void catalog_get_repr_item_from_record (CATALOG_REPR_ITEM * item_p, char *rec_p);
static void catalog_put_repr_item_to_record (char *rec_p, CATALOG_REPR_ITEM * item_p);
static int catalog_assign_attribute (THREAD_ENTRY * thread_p, DISK_ATTR * disk_attr_p,
				     CATALOG_RECORD * catalog_record_p);

#if defined (SA_MODE)
static int catalog_file_map_is_empty (THREAD_ENTRY * thread_p, PAGE_PTR * page, bool * stop, void *args);
#endif /* SA_MODE */
static int catalog_file_map_page_dump (THREAD_ENTRY * thread_p, PAGE_PTR * page, bool * stop, void *args);
static int catalog_file_map_overflow_count (THREAD_ENTRY * thread_p, PAGE_PTR * page, bool * stop, void *args);

static void
catalog_put_page_header (char *rec_p, CATALOG_PAGE_HEADER * header_p)
{
  CATALOG_PUT_PGHEADER_OVFL_PGID_PAGEID (rec_p, header_p->overflow_page_id.pageid);
  CATALOG_PUT_PGHEADER_OVFL_PGID_VOLID (rec_p, header_p->overflow_page_id.volid);
  CATALOG_PUT_PGHEADER_DIR_COUNT (rec_p, header_p->dir_count);
  CATALOG_PUT_PGHEADER_PG_OVFL (rec_p, header_p->is_overflow_page);
}

static void
catalog_get_disk_representation (DISK_REPR * disk_repr_p, char *rec_p)
{
  disk_repr_p->id = (REPR_ID) OR_GET_INT (rec_p + CATALOG_DISK_REPR_ID_OFF);
  disk_repr_p->n_fixed = OR_GET_INT (rec_p + CATALOG_DISK_REPR_N_FIXED_OFF);
  disk_repr_p->fixed = NULL;
  disk_repr_p->fixed_length = OR_GET_INT (rec_p + CATALOG_DISK_REPR_FIXED_LENGTH_OFF);
  disk_repr_p->n_variable = OR_GET_INT (rec_p + CATALOG_DISK_REPR_N_VARIABLE_OFF);
  disk_repr_p->variable = NULL;

#if 0				/* reserved for future use */
  disk_repr_p->repr_reserved_1 = OR_GET_INT (rec_p + CATALOG_DISK_REPR_RESERVED_1_OFF);
#endif
}

static void
catalog_put_disk_representation (char *rec_p, DISK_REPR * disk_repr_p)
{
  OR_PUT_INT (rec_p + CATALOG_DISK_REPR_ID_OFF, disk_repr_p->id);
  OR_PUT_INT (rec_p + CATALOG_DISK_REPR_N_FIXED_OFF, disk_repr_p->n_fixed);
  OR_PUT_INT (rec_p + CATALOG_DISK_REPR_FIXED_LENGTH_OFF, disk_repr_p->fixed_length);
  OR_PUT_INT (rec_p + CATALOG_DISK_REPR_N_VARIABLE_OFF, disk_repr_p->n_variable);

#if 1				/* reserved for future use */
  OR_PUT_INT (rec_p + CATALOG_DISK_REPR_RESERVED_1_OFF, 0);
#endif
}

static void
catalog_get_disk_attribute (DISK_ATTR * attr_p, char *rec_p)
{
  attr_p->id = OR_GET_INT (rec_p + CATALOG_DISK_ATTR_ID_OFF);
  attr_p->location = OR_GET_INT (rec_p + CATALOG_DISK_ATTR_LOCATION_OFF);
  attr_p->type = (DB_TYPE) OR_GET_INT (rec_p + CATALOG_DISK_ATTR_TYPE_OFF);
  attr_p->value = NULL;
  attr_p->val_length = OR_GET_INT (rec_p + CATALOG_DISK_ATTR_VAL_LENGTH_OFF);
  attr_p->position = OR_GET_INT (rec_p + CATALOG_DISK_ATTR_POSITION_OFF);
  attr_p->default_expr = DB_DEFAULT_NONE;

  OR_GET_OID (rec_p + CATALOG_DISK_ATTR_CLASSOID_OFF, &attr_p->classoid);
  attr_p->n_btstats = OR_GET_INT (rec_p + CATALOG_DISK_ATTR_N_BTSTATS_OFF);
  attr_p->bt_stats = NULL;
}

static void
catalog_get_btree_statistics (BTREE_STATS * stat_p, char *rec_p)
{
  int i;

  stat_p->leafs = OR_GET_INT (rec_p + CATALOG_BT_STATS_LEAFS_OFF);
  stat_p->pages = OR_GET_INT (rec_p + CATALOG_BT_STATS_PAGES_OFF);
  stat_p->height = OR_GET_INT (rec_p + CATALOG_BT_STATS_HEIGHT_OFF);
  stat_p->keys = OR_GET_INT (rec_p + CATALOG_BT_STATS_KEYS_OFF);
  stat_p->has_function = OR_GET_INT (rec_p + CATALOG_BT_STATS_FUNC_INDEX_OFF);

  assert (stat_p->pkeys_size <= BTREE_STATS_PKEYS_NUM);
  for (i = 0; i < stat_p->pkeys_size; i++)
    {
      stat_p->pkeys[i] = OR_GET_INT (rec_p + CATALOG_BT_STATS_PKEYS_OFF + (OR_INT_SIZE * i));
    }
#if 0				/* reserved for future use */
  for (i = 0; i < BTREE_STATS_RESERVED_NUM; i++)
    {
      stat_p->reserved[i] = OR_GET_INT (rec_p + CATALOG_BT_STATS_RESERVED_OFF + (OR_INT_SIZE * i));
    }
#endif
}

static void
catalog_put_disk_attribute (char *rec_p, DISK_ATTR * attr_p)
{
  OR_PUT_INT (rec_p + CATALOG_DISK_ATTR_ID_OFF, attr_p->id);
  OR_PUT_INT (rec_p + CATALOG_DISK_ATTR_LOCATION_OFF, attr_p->location);
  OR_PUT_INT (rec_p + CATALOG_DISK_ATTR_TYPE_OFF, attr_p->type);
  OR_PUT_INT (rec_p + CATALOG_DISK_ATTR_VAL_LENGTH_OFF, attr_p->val_length);
  OR_PUT_INT (rec_p + CATALOG_DISK_ATTR_POSITION_OFF, attr_p->position);

  OR_PUT_OID (rec_p + CATALOG_DISK_ATTR_CLASSOID_OFF, &attr_p->classoid);
  OR_PUT_INT (rec_p + CATALOG_DISK_ATTR_N_BTSTATS_OFF, attr_p->n_btstats);
}

static void
catalog_put_btree_statistics (char *rec_p, BTREE_STATS * stat_p)
{
  int i;

  OR_PUT_BTID (rec_p + CATALOG_BT_STATS_BTID_OFF, &stat_p->btid);
  OR_PUT_INT (rec_p + CATALOG_BT_STATS_LEAFS_OFF, stat_p->leafs);
  OR_PUT_INT (rec_p + CATALOG_BT_STATS_PAGES_OFF, stat_p->pages);
  OR_PUT_INT (rec_p + CATALOG_BT_STATS_HEIGHT_OFF, stat_p->height);
  OR_PUT_INT (rec_p + CATALOG_BT_STATS_KEYS_OFF, stat_p->keys);
  OR_PUT_INT (rec_p + CATALOG_BT_STATS_FUNC_INDEX_OFF, stat_p->has_function);

  assert (stat_p->pkeys_size <= BTREE_STATS_PKEYS_NUM);
  for (i = 0; i < stat_p->pkeys_size; i++)
    {
      OR_PUT_INT (rec_p + CATALOG_BT_STATS_PKEYS_OFF + (OR_INT_SIZE * i), stat_p->pkeys[i]);
    }

#if 1				/* reserved for future use */
  for (i = 0; i < BTREE_STATS_RESERVED_NUM; i++)
    {
      OR_PUT_INT (rec_p + CATALOG_BT_STATS_RESERVED_OFF + (OR_INT_SIZE * i), 0);
    }
#endif
}

static void
catalog_get_class_info_from_record (CLS_INFO * class_info_p, char *rec_p)
{
  OR_GET_HFID (rec_p + CATALOG_CLS_INFO_HFID_OFF, &class_info_p->ci_hfid);

  class_info_p->ci_tot_pages = OR_GET_INT (rec_p + CATALOG_CLS_INFO_TOT_PAGES_OFF);
  class_info_p->ci_tot_objects = OR_GET_INT (rec_p + CATALOG_CLS_INFO_TOT_OBJS_OFF);
  class_info_p->ci_time_stamp = OR_GET_INT (rec_p + CATALOG_CLS_INFO_TIME_STAMP_OFF);

  OR_GET_OID (rec_p + CATALOG_CLS_INFO_REP_DIR_OFF, &(class_info_p->ci_rep_dir));
  assert (!OID_ISNULL (&(class_info_p->ci_rep_dir)));
}

static void
catalog_put_class_info_to_record (char *rec_p, CLS_INFO * class_info_p)
{
  assert (!OID_ISNULL (&(class_info_p->ci_rep_dir)));

  OR_PUT_HFID (rec_p + CATALOG_CLS_INFO_HFID_OFF, &class_info_p->ci_hfid);

  OR_PUT_INT (rec_p + CATALOG_CLS_INFO_TOT_PAGES_OFF, class_info_p->ci_tot_pages);
  OR_PUT_INT (rec_p + CATALOG_CLS_INFO_TOT_OBJS_OFF, class_info_p->ci_tot_objects);
  OR_PUT_INT (rec_p + CATALOG_CLS_INFO_TIME_STAMP_OFF, class_info_p->ci_time_stamp);

  OR_PUT_OID (rec_p + CATALOG_CLS_INFO_REP_DIR_OFF, &(class_info_p->ci_rep_dir));
}

static void
catalog_get_repr_item_from_record (CATALOG_REPR_ITEM * item_p, char *rec_p)
{
  item_p->page_id.pageid = OR_GET_INT (rec_p + CATALOG_REPR_ITEM_PAGEID_PAGEID_OFF);
  item_p->page_id.volid = OR_GET_SHORT (rec_p + CATALOG_REPR_ITEM_PAGEID_VOLID_OFF);
  item_p->repr_id = OR_GET_SHORT (rec_p + CATALOG_REPR_ITEM_REPRID_OFF);
  item_p->slot_id = OR_GET_SHORT (rec_p + CATALOG_REPR_ITEM_SLOTID_OFF);
}

static void
catalog_put_repr_item_to_record (char *rec_p, CATALOG_REPR_ITEM * item_p)
{
  OR_PUT_INT (rec_p + CATALOG_REPR_ITEM_PAGEID_PAGEID_OFF, item_p->page_id.pageid);
  OR_PUT_SHORT (rec_p + CATALOG_REPR_ITEM_PAGEID_VOLID_OFF, item_p->page_id.volid);
  OR_PUT_SHORT (rec_p + CATALOG_REPR_ITEM_REPRID_OFF, item_p->repr_id);
  OR_PUT_SHORT (rec_p + CATALOG_REPR_ITEM_SLOTID_OFF, item_p->slot_id);
}

static void
catalog_initialize_max_space (CATALOG_MAX_SPACE * max_space_p)
{
  int rv;
  rv = pthread_mutex_lock (&catalog_Max_space_lock);

  max_space_p->max_page_id.pageid = NULL_PAGEID;
  max_space_p->max_page_id.volid = NULL_VOLID;
  max_space_p->max_space = -1;

  pthread_mutex_unlock (&catalog_Max_space_lock);
}

static void
catalog_update_max_space (VPID * page_id_p, PGLENGTH space)
{
  int rv;
  rv = pthread_mutex_lock (&catalog_Max_space_lock);

  if (VPID_EQ (page_id_p, &catalog_Max_space.max_page_id))
    {
      catalog_Max_space.max_space = space;
    }
  else if (space > catalog_Max_space.max_space)
    {
      catalog_Max_space.max_page_id = *(page_id_p);
      catalog_Max_space.max_space = space;
    }

  pthread_mutex_unlock (&catalog_Max_space_lock);
}

/*
 * catalog_initialize_new_page () -
 *   return:
 *   vfid(in):
 *   file_type(in):
 *   vpid(in):
 *   ignore_npages(in):
 *   pg_ovfl(in):
 */
/*
 * catalog_initialize_new_page () - document me!
 *
 * return        : Error code
 * thread_p (in) : Thread entry
 * page (in)     : New catalog page
 * args (in)     : bool * (is_overflow_page)
 */
static int
catalog_initialize_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page, void *args)
{
  CATALOG_PAGE_HEADER page_header;
  PGSLOTID slot_id;
  int success;
  RECDES record = {
    CATALOG_PAGE_HEADER_SIZE, CATALOG_PAGE_HEADER_SIZE, REC_HOME, NULL
  };
  char data[CATALOG_PAGE_HEADER_SIZE + MAX_ALIGNMENT], *aligned_data;
  bool is_overflow_page = *(bool *) args;

  aligned_data = PTR_ALIGN (data, MAX_ALIGNMENT);

  pgbuf_set_page_ptype (thread_p, page, PAGE_CATALOG);
  spage_initialize (thread_p, page, ANCHORED_DONT_REUSE_SLOTS, MAX_ALIGNMENT, SAFEGUARD_RVSPACE);

  VPID_SET_NULL (&page_header.overflow_page_id);
  page_header.dir_count = 0;
  page_header.is_overflow_page = is_overflow_page;

  recdes_set_data_area (&record, aligned_data, CATALOG_PAGE_HEADER_SIZE);
  catalog_put_page_header (record.data, &page_header);

  success = spage_insert (thread_p, page, &record, &slot_id);
  if (success != SP_SUCCESS || slot_id != CATALOG_HEADER_SLOT)
    {
      assert (false);
      if (success != SP_SUCCESS)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	}
      return ER_FAILED;
    }

  log_append_redo_data2 (thread_p, RVCT_NEWPAGE, NULL, page, -1, sizeof (CATALOG_PAGE_HEADER), &page_header);

  pgbuf_set_dirty (thread_p, page, DONT_FREE);
  return NO_ERROR;
}

/*
 * catalog_get_new_page () - Get a new page for the catalog
 *   return: The pointer to a newly allocated page, or NULL
 *           The parameter page_id is set to the page identifier.
 *   page_id(out): Set to the page identifier for the newly allocated page
 *   nearpg(in): A page identifier that may be used in a nearby page allocation.
 *               (It may be ignored.)
 *   pg_ovfl(in): Page is an overflow page (1) or not (0)
 *
 * Note: Allocates a new page for the catalog and inserts the header
 * record for the page.
 */
static PAGE_PTR
catalog_get_new_page (THREAD_ENTRY * thread_p, VPID * page_id_p, bool is_overflow_page)
{
  PAGE_PTR page_p;

  log_sysop_start (thread_p);

  if (file_alloc (thread_p, &catalog_Id.vfid, catalog_initialize_new_page, &is_overflow_page, page_id_p, &page_p)
      != NO_ERROR)
    {
      ASSERT_ERROR ();
      log_sysop_abort (thread_p);
      return NULL;
    }
  if (page_p == NULL)
    {
      assert_release (false);
      log_sysop_abort (thread_p);
      return NULL;
    }
  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);
  log_sysop_commit (thread_p);

  return page_p;
}

/*
 * catalog_file_map_find_optimal_page () - FILE_MAP_PAGE_FUNC function that checks a catalog page has enough space
 *                                         for a new record.
 *
 * return        : error code
 * thread_p (in) : thread entry
 * page (in/out) : page to check. if page has enough space, its value is moved to context and the output of this pointer
 *                 is NULL.
 * stop (out)    : output true if page has enough space
 * args (in/out) : find optimal page context
 */
static int
catalog_file_map_find_optimal_page (THREAD_ENTRY * thread_p, PAGE_PTR * page, bool * stop, void *args)
{
  CATALOG_FIND_OPTIMAL_PAGE_CONTEXT *context = (CATALOG_FIND_OPTIMAL_PAGE_CONTEXT *) args;
  RECDES record;
  int dir_count;

  (void) pgbuf_check_page_ptype (thread_p, *page, PAGE_CATALOG);

  if (spage_get_record (*page, CATALOG_HEADER_SLOT, &record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }
  if (CATALOG_GET_PGHEADER_PG_OVFL (record.data) || CATALOG_GET_PGHEADER_OVFL_PGID_PAGEID (record.data) != NULL_PAGEID)
    {
      /* overflow page */
      return NO_ERROR;
    }
  context->size_optimal_free = spage_max_space_for_new_record (thread_p, *page) - CATALOG_MAX_SLOT_ID_SIZE;
  dir_count = CATALOG_GET_PGHEADER_DIR_COUNT (record.data);
  if (dir_count > 0)
    {
      context->size_optimal_free -= (int) (DB_PAGESIZE * (0.25f + (dir_count - 1) * 0.05f));
    }
  if (context->size_optimal_free > context->size)
    {
      /* found optimal page */
      pgbuf_get_vpid (*page, &context->vpid_optimal);
      context->page_optimal = *page;
      *page = NULL;
      *stop = true;
    }
  return NO_ERROR;
}

/*
 * catalog_find_optimal_page () -
 *   return: PAGE_PTR
 *   size(in): The size requested in the page
 *   page_id(out): Set to the page identifier fetched
 */
static PAGE_PTR
catalog_find_optimal_page (THREAD_ENTRY * thread_p, int size, VPID * page_id_p)
{
  PAGE_PTR page_p;
  CATALOG_FIND_OPTIMAL_PAGE_CONTEXT context;

  assert (page_id_p != NULL);

  context.size = size;
  VPID_SET_NULL (&context.vpid_optimal);
  context.page_optimal = NULL;

  pthread_mutex_lock (&catalog_Max_space_lock);

  if (catalog_Max_space.max_page_id.pageid != NULL_PAGEID && catalog_Max_space.max_space > size)
    {
      /* try to use page hint */
      bool can_use = false;

      *page_id_p = catalog_Max_space.max_page_id;
      pthread_mutex_unlock (&catalog_Max_space_lock);

      page_p = pgbuf_fix (thread_p, page_id_p, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_p == NULL)
	{
	  ASSERT_ERROR ();
	  return NULL;
	}
      if (catalog_file_map_find_optimal_page (thread_p, &page_p, &can_use, &context) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return NULL;
	}
      if (can_use)
	{
	  /* we can use cached page. careful, it was moved to context. */
	  assert (!VPID_ISNULL (&context.vpid_optimal));
	  assert (context.page_optimal != NULL);
	  assert (page_p == NULL);

	  *page_id_p = context.vpid_optimal;
	  return context.page_optimal;
	}
      else
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	}

      /* need another page */
      pthread_mutex_lock (&catalog_Max_space_lock);
    }

  /* todo: search the file table for enough empty space. this system is not ideal at all! but we have no other means
   *       of reusing free space. we'll need a different design for catalog file or a free space map or anything.
   *       maybe we'll consider catalog when we'll rethink the best space system of heap file.
   */

  if (file_map_pages (thread_p, &catalog_Id.vfid, PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH,
		      catalog_file_map_find_optimal_page, &context) != NO_ERROR)
    {
      ASSERT_ERROR ();
      pthread_mutex_unlock (&catalog_Max_space_lock);
      return NULL;
    }

  if (context.page_optimal != NULL)
    {
      if (catalog_Max_space.max_page_id.pageid == NULL_PAGEID
	  || catalog_Max_space.max_space < context.size_optimal_free - size)
	{
	  /* replace entry in max space */
	  catalog_Max_space.max_page_id = context.vpid_optimal;
	  catalog_Max_space.max_space = context.size_optimal_free;
	}

      pthread_mutex_unlock (&catalog_Max_space_lock);

      *page_id_p = context.vpid_optimal;
      return context.page_optimal;
    }

  /* no page with enough space was found */
  page_p = catalog_get_new_page (thread_p, page_id_p, false);
  if (page_p == NULL)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  catalog_Max_space.max_page_id = *page_id_p;
  catalog_Max_space.max_space = spage_max_space_for_new_record (thread_p, page_p) - CATALOG_MAX_SLOT_ID_SIZE;
  pthread_mutex_unlock (&catalog_Max_space_lock);

  return page_p;
}

/*
 * catalog_free_representation () - Free disk representation memory area
 *   return: nothing
 *   dr(in): Disk representation structure pointer
 */
void
catalog_free_representation (DISK_REPR * repr_p)
{
  int attr_cnt, k, j;
  DISK_ATTR *attr_p;
  BTREE_STATS *stat_p;

  if (repr_p != NULL)
    {
      attr_cnt = repr_p->n_fixed + repr_p->n_variable;
      for (k = 0; k < attr_cnt; k++)
	{
	  attr_p = ((k < repr_p->n_fixed)
		    ? (DISK_ATTR *) repr_p->fixed + k : (DISK_ATTR *) repr_p->variable + (k - repr_p->n_fixed));

	  if (attr_p->value != NULL)
	    {
	      db_private_free_and_init (NULL, attr_p->value);
	    }

	  if (attr_p->bt_stats != NULL)
	    {
	      for (j = 0; j < attr_p->n_btstats; j++)
		{
		  stat_p = &(attr_p->bt_stats[j]);
		  if (stat_p->pkeys != NULL)
		    {
		      db_private_free_and_init (NULL, stat_p->pkeys);
		    }
		}
	      db_private_free_and_init (NULL, attr_p->bt_stats);
	    }
	}

      if (repr_p->fixed != NULL)
	{
	  db_private_free_and_init (NULL, repr_p->fixed);
	}

      if (repr_p->variable != NULL)
	{
	  db_private_free_and_init (NULL, repr_p->variable);
	}

      db_private_free_and_init (NULL, repr_p);
    }
}

/*
 * catalog_free_class_info () - Free class specific information memory area
 *   return: nothing
 *   cls_info(in): Pointer to the class information structure
 */
void
catalog_free_class_info (CLS_INFO * class_info_p)
{
  if (class_info_p)
    {
      db_private_free_and_init (NULL, class_info_p);
    }
}

/*
 * catalog_get_key_list () -
 *   return: NO_ERROR or error code
 *   key(in):
 *   val(in):
 *   args(in):
 */
static int
catalog_get_key_list (THREAD_ENTRY * thread_p, void *key, void *ignore_value, void *args)
{
  CATALOG_CLASS_ID_LIST *class_id_p, **p;

  p = (CATALOG_CLASS_ID_LIST **) args;

  class_id_p = (CATALOG_CLASS_ID_LIST *) db_private_alloc (thread_p, sizeof (CATALOG_CLASS_ID_LIST));

  if (class_id_p == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  class_id_p->class_id.volid = ((OID *) key)->volid;
  class_id_p->class_id.pageid = ((OID *) key)->pageid;
  class_id_p->class_id.slotid = ((OID *) key)->slotid;
  class_id_p->next = *p;

  *p = class_id_p;

  return NO_ERROR;
}

/*
 * catalog_free_key_list () -
 *   return:
 *   clsid_list(in):
 */
static void
catalog_free_key_list (CATALOG_CLASS_ID_LIST * class_id_list)
{
  CATALOG_CLASS_ID_LIST *p, *next;

  if (class_id_list == NULL)
    {
      return;
    }

  for (p = class_id_list; p; p = next)
    {
      next = p->next;
      db_private_free_and_init (NULL, p);
    }

  class_id_list = NULL;
}

/*
 * catalog_entry_alloc () - allocate a catalog entry
 *   returns: new pointer or NULL on error
 */
static void *
catalog_entry_alloc (void)
{
  return malloc (sizeof (CATALOG_ENTRY));
}

/*
 * catalog_entry_free () - free a catalog entry
 *   returns: error code or NO_ERROR
 *   ent(in): entry to free
 */
static int
catalog_entry_free (void *ent)
{
  free (ent);
  return NO_ERROR;
}

/*
 * catalog_entry_init () - initialize a catalog entry
 *   returns: error code or NO_ERROR
 *   ent(in): entry to initialize
 */
static int
catalog_entry_init (void *ent)
{
  /* TO BE FILLED IN IF NECESSARY */
  return NO_ERROR;
}

/*
 * catalog_entry_uninit () - uninitialize a catalog entry
 *   returns: error code or NO_ERROR
 *   ent(in): entry to uninitialize
 */
static int
catalog_entry_uninit (void *ent)
{
  /* TO BE FILLED IN IF NECESSARY */
  return NO_ERROR;
}

/*
 * catalog_key_copy () - copy a key
 *   returns: error code or NO_ERROR
 *   src(in): source key
 *   dest(in): destination key
 */
static int
catalog_key_copy (void *src, void *dest)
{
  CATALOG_KEY *src_k = (CATALOG_KEY *) src;
  CATALOG_KEY *dest_k = (CATALOG_KEY *) dest;

  if (src_k == NULL || dest_k == NULL)
    {
      return ER_FAILED;
    }

  /* copy key members */
  dest_k->page_id = src_k->page_id;
  dest_k->repr_id = src_k->repr_id;
  dest_k->slot_id = src_k->slot_id;
  dest_k->volid = src_k->volid;

  /* copy data members */
  VPID_COPY (&dest_k->r_page_id, &src_k->r_page_id);
  dest_k->r_slot_id = src_k->r_slot_id;

  return NO_ERROR;
}

/*
 * catalog_compare () - Compare two catalog keys
 *   return: int (true or false)
 *   key1(in): First catalog key
 *   key2(in): Second catalog key
 */
static int
catalog_key_compare (void *key1, void *key2)
{
  const CATALOG_KEY *k1, *k2;

  k1 = (const CATALOG_KEY *) key1;
  k2 = (const CATALOG_KEY *) key2;

  /* only compare key members */
  if (k1->page_id == k2->page_id && k1->slot_id == k2->slot_id && k1->repr_id == k2->repr_id && k1->volid == k2->volid)
    {
      /* equal */
      return 0;
    }
  else
    {
      /* not equal */
      return 1;
    }
}

/*
 * catalog_hash () -
 *   return: int
 *   key(in): Catalog key
 *   htsize(in): Memory Hash Table Size
 *
 * Note: Generate a hash number for the given key for the given hash table size.
 */
static unsigned int
catalog_key_hash (void *key, int hash_table_size)
{
  const CATALOG_KEY *k1 = (const CATALOG_KEY *) key;
  unsigned int hash_res;

  hash_res =
    ((((k1)->slot_id | ((k1)->page_id << 8)) ^ (((k1)->page_id >> 8) | (((PAGEID) (k1)->volid) << 24))) + k1->repr_id);

  return (hash_res % hash_table_size);
}

/*
 * catalog_put_record_into_page () -
 *   return: NO_ERROR or ER_FAILED
 *   ct_recordp(in): pointer to CATALOG_RECORD structure
 *   next(in): flag of next page
 *   remembered_slotid(in):
 *
 * Note: Put the catalog record into the page and then prepare next page.
 */
static int
catalog_put_record_into_page (THREAD_ENTRY * thread_p, CATALOG_RECORD * catalog_record_p, int next,
			      PGSLOTID * remembered_slot_id_p)
{
  PAGE_PTR new_page_p;
  VPID new_vpid;

  /* if some space in 'recdes'data' is remained */
  if (catalog_record_p->offset < catalog_record_p->recdes.area_size)
    {
      return NO_ERROR;
    }

  if (spage_insert (thread_p, catalog_record_p->page_p, &catalog_record_p->recdes, &catalog_record_p->slotid) !=
      SP_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, catalog_record_p->page_p);
      return ER_FAILED;
    }

  if (*remembered_slot_id_p == NULL_SLOTID)
    {
      *remembered_slot_id_p = catalog_record_p->slotid;
    }

  log_append_undoredo_recdes2 (thread_p, RVCT_INSERT, &catalog_Id.vfid, catalog_record_p->page_p,
			       catalog_record_p->slotid, NULL, &catalog_record_p->recdes);
  pgbuf_set_dirty (thread_p, catalog_record_p->page_p, DONT_FREE);

  /* if there's no need to get next page; when this is the last page */
  if (!next)
    {
      catalog_record_p->slotid = *remembered_slot_id_p;
      *remembered_slot_id_p = NULL_SLOTID;
      return NO_ERROR;
    }

  new_page_p = catalog_get_new_page (thread_p, &new_vpid, true);
  if (new_page_p == NULL)
    {
      pgbuf_unfix_and_init (thread_p, catalog_record_p->page_p);
      return ER_FAILED;
    }

  log_append_undo_data2 (thread_p, RVCT_NEW_OVFPAGE_LOGICAL_UNDO, &catalog_Id.vfid, NULL, -1, sizeof (new_vpid),
			 &new_vpid);

  /* make the previous page point to the newly allocated page */
  catalog_record_p->recdes.area_size = DB_PAGESIZE;
  (void) spage_get_record (catalog_record_p->page_p, CATALOG_HEADER_SLOT, &catalog_record_p->recdes, COPY);

  log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, catalog_record_p->page_p, CATALOG_HEADER_SLOT,
			   &catalog_record_p->recdes);

  CATALOG_PUT_PGHEADER_OVFL_PGID_PAGEID (catalog_record_p->recdes.data, new_vpid.pageid);
  CATALOG_PUT_PGHEADER_OVFL_PGID_VOLID (catalog_record_p->recdes.data, new_vpid.volid);

  if (spage_update (thread_p, catalog_record_p->page_p, CATALOG_HEADER_SLOT, &catalog_record_p->recdes) != SP_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, catalog_record_p->page_p);
      return ER_FAILED;
    }

  log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, catalog_record_p->page_p, CATALOG_HEADER_SLOT,
			   &catalog_record_p->recdes);

  pgbuf_set_dirty (thread_p, catalog_record_p->page_p, FREE);

  catalog_record_p->vpid.pageid = new_vpid.pageid;
  catalog_record_p->vpid.volid = new_vpid.volid;
  catalog_record_p->page_p = new_page_p;
  catalog_record_p->recdes.area_size = spage_max_space_for_new_record (thread_p, new_page_p) - CATALOG_MAX_SLOT_ID_SIZE;
  catalog_record_p->recdes.length = 0;
  catalog_record_p->offset = 0;

  return NO_ERROR;
}

static int
catalog_write_unwritten_portion (THREAD_ENTRY * thread_p, CATALOG_RECORD * catalog_record_p,
				 PGSLOTID * remembered_slot_id_p, int format_size)
{
  /* if the remained, unwritten portion of the record data is smaller than the size of disk format of structure */
  if (catalog_record_p->recdes.area_size - catalog_record_p->offset < format_size)
    {
      /* set the record length as the current offset, the size of written portion of the record data, and set the
       * offset to the end of the record data to write the page */
      catalog_record_p->recdes.length = catalog_record_p->offset;
      catalog_record_p->offset = catalog_record_p->recdes.area_size;

      if (catalog_put_record_into_page (thread_p, catalog_record_p, 1, remembered_slot_id_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * catalog_store_disk_representation () -
 *   return: NO_ERROR or ER_FAILED
 *   disk_reprp(in): pointer to DISK_REPR structure (disk representation)
 *   ct_recordp(in): pointer to CATALOG_RECORD structure (catalog record)
 *   remembered_slotid(in):
 *
 * Note: Transforms disk representation form into catalog disk form.
 * Store DISK_REPR structure into catalog record.
 */
static int
catalog_store_disk_representation (THREAD_ENTRY * thread_p, DISK_REPR * disk_repr_p, CATALOG_RECORD * catalog_record_p,
				   PGSLOTID * remembered_slot_id_p)
{
  if (catalog_write_unwritten_portion (thread_p, catalog_record_p, remembered_slot_id_p, CATALOG_DISK_REPR_SIZE) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  catalog_put_disk_representation (catalog_record_p->recdes.data + catalog_record_p->offset, disk_repr_p);
  catalog_record_p->offset += CATALOG_DISK_REPR_SIZE;

  return NO_ERROR;
}

/*
 * catalog_store_disk_attribute () -
 *     NO_ERROR or ER_FAILED
 *   disk_attrp(in): pointer to DISK_ATTR structure (disk representation)
 *   ct_recordp(in): pointer to CATALOG_RECORD structure (catalog record)
 *   remembered_slotid(in):
 *
 * Note: Transforms disk representation form into catalog disk form.
 * Store DISK_ATTR structure into catalog record.
 */
static int
catalog_store_disk_attribute (THREAD_ENTRY * thread_p, DISK_ATTR * disk_attr_p, CATALOG_RECORD * catalog_record_p,
			      PGSLOTID * remembered_slot_id_p)
{
  if (catalog_write_unwritten_portion (thread_p, catalog_record_p, remembered_slot_id_p, CATALOG_DISK_ATTR_SIZE) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  catalog_put_disk_attribute (catalog_record_p->recdes.data + catalog_record_p->offset, disk_attr_p);
  catalog_record_p->offset += CATALOG_DISK_ATTR_SIZE;

  return NO_ERROR;
}

/*
 * catalog_store_attribute_value () -
 *   return: NO_ERROR or ER_FAILED
 *   value(in): pointer to the value data (disk representation)
 *   length(in): length of the value data
 *   ct_recordp(in): pointer to CATALOG_RECORD structure (catalog record)
 *   remembered_slotid(in):
 *
 * Note: Transforms disk representation form into catalog disk form.
 * Store value data into catalog record.
 */
static int
catalog_store_attribute_value (THREAD_ENTRY * thread_p, void *value, int length, CATALOG_RECORD * catalog_record_p,
			       PGSLOTID * remembered_slot_id_p)
{
  int offset = 0;
  int bufsize;

  if (catalog_write_unwritten_portion (thread_p, catalog_record_p, remembered_slot_id_p, length) != NO_ERROR)
    {
      return ER_FAILED;
    }

  while (offset < length)
    {
      if (length - offset <= catalog_record_p->recdes.area_size - catalog_record_p->offset)
	{
	  /* if the size of the value is smaller than or equals to the remaining size of the recdes.data, just copy the 
	   * value into the recdes.data buffer and adjust the offset. */
	  bufsize = length - offset;
	  (void) memcpy (catalog_record_p->recdes.data + catalog_record_p->offset, (char *) value + offset, bufsize);
	  catalog_record_p->offset += bufsize;
	  break;
	}
      else
	{
	  /* if the size of the value is larger than the whole size of the recdes.data, we need split the value over N
	   * pages. The first N-1 pages need to be stored into pages, while the last page can be stored in the
	   * recdes.data buffer as the existing routine. */
	  assert (catalog_record_p->offset == 0);
	  bufsize = catalog_record_p->recdes.area_size;
	  (void) memcpy (catalog_record_p->recdes.data, (char *) value + offset, bufsize);
	  offset += bufsize;

	  /* write recdes.data and fill catalog_record_p as new page */
	  catalog_record_p->offset = catalog_record_p->recdes.area_size;
	  catalog_record_p->recdes.length = catalog_record_p->offset;
	  if (catalog_put_record_into_page (thread_p, catalog_record_p, 1, remembered_slot_id_p) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
    }
  return NO_ERROR;
}

/*
 * catalog_store_btree_statistics () -
 *   return: NO_ERROR or ER_FAILED
 *   bt_statsp(in): pointer to BTREE_STATS structure (disk representation)
 *   ct_recordp(in): pointer to CATALOG_RECORD structure (catalog record)
 *   remembered_slotid(in):
 *
 * Note: Transforms disk representation form into catalog disk form.
 * Store BTREE_STATS structure into catalog record.
 */
static int
catalog_store_btree_statistics (THREAD_ENTRY * thread_p, BTREE_STATS * btree_stats_p, CATALOG_RECORD * catalog_record_p,
				PGSLOTID * remembered_slot_id_p)
{
  if (catalog_write_unwritten_portion (thread_p, catalog_record_p, remembered_slot_id_p, CATALOG_BT_STATS_SIZE) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  catalog_put_btree_statistics (catalog_record_p->recdes.data + catalog_record_p->offset, btree_stats_p);
  catalog_record_p->offset += CATALOG_BT_STATS_SIZE;

  return NO_ERROR;
}

/*
 * catalog_get_record_from_page () - Get the catalog record from the page.
 *   return: NO_ERROR or ER_FAILED
 *   ct_recordp(in): pointer to CATALOG_RECORD structure
 */
static int
catalog_get_record_from_page (THREAD_ENTRY * thread_p, CATALOG_RECORD * catalog_record_p)
{
  /* if some data in 'recdes.data' is remained */
  if (catalog_record_p->offset < catalog_record_p->recdes.length)
    {
      return NO_ERROR;
    }

  /* if it is not first time, if there was the page previously read */
  if (catalog_record_p->page_p)
    {
      if (spage_get_record (catalog_record_p->page_p, CATALOG_HEADER_SLOT, &catalog_record_p->recdes, PEEK) !=
	  S_SUCCESS)
	{
	  return ER_FAILED;
	}

      catalog_record_p->vpid.pageid = CATALOG_GET_PGHEADER_OVFL_PGID_PAGEID (catalog_record_p->recdes.data);
      catalog_record_p->vpid.volid = CATALOG_GET_PGHEADER_OVFL_PGID_VOLID (catalog_record_p->recdes.data);
      catalog_record_p->slotid = 1;

      pgbuf_unfix_and_init (thread_p, catalog_record_p->page_p);
    }

  if (catalog_record_p->vpid.pageid == NULL_PAGEID || catalog_record_p->vpid.volid == NULL_VOLID)
    {
      return ER_FAILED;
    }

  catalog_record_p->page_p =
    pgbuf_fix (thread_p, &catalog_record_p->vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (catalog_record_p->page_p == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, catalog_record_p->page_p, PAGE_CATALOG);

  if (spage_get_record (catalog_record_p->page_p, catalog_record_p->slotid, &catalog_record_p->recdes, PEEK) !=
      S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, catalog_record_p->page_p);
      return ER_FAILED;
    }

  catalog_record_p->offset = 0;
  return NO_ERROR;
}

static int
catalog_read_unread_portion (THREAD_ENTRY * thread_p, CATALOG_RECORD * catalog_record_p, int format_size)
{
  if (catalog_record_p->recdes.length - catalog_record_p->offset < format_size)
    {
      catalog_record_p->offset = catalog_record_p->recdes.length;
      if (catalog_get_record_from_page (thread_p, catalog_record_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * catalog_fetch_disk_representation () -
 *   return: NO_ERROR or ER_FAILED
 *   disk_reprp(in): pointer to DISK_REPR structure (disk representation)
 *   ct_recordp(in): pointer to CATALOG_RECORD structure (catalog record)
 *
 * Note: Transforms catalog disk form into disk representation form.
 * Fetch DISK_REPR structure from catalog record.
 */
static int
catalog_fetch_disk_representation (THREAD_ENTRY * thread_p, DISK_REPR * disk_repr_p, CATALOG_RECORD * catalog_record_p)
{
  if (catalog_read_unread_portion (thread_p, catalog_record_p, CATALOG_DISK_REPR_SIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  catalog_get_disk_representation (disk_repr_p, catalog_record_p->recdes.data + catalog_record_p->offset);
  catalog_record_p->offset += CATALOG_DISK_REPR_SIZE;

  return NO_ERROR;
}

/*
 * catalog_fetch_disk_attribute () -
 *   return: NO_ERROR or ER_FAILED
 *   disk_attrp(in): pointer to DISK_ATTR structure (disk representation)
 *   ct_recordp(in): pointer to CATALOG_RECORD structure (catalog record)
 *
 * Note: Transforms catalog disk form into disk representation form.
 * Fetch DISK_ATTR structure from catalog record.
 */
static int
catalog_fetch_disk_attribute (THREAD_ENTRY * thread_p, DISK_ATTR * disk_attr_p, CATALOG_RECORD * catalog_record_p)
{
  if (catalog_read_unread_portion (thread_p, catalog_record_p, CATALOG_DISK_ATTR_SIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  catalog_get_disk_attribute (disk_attr_p, catalog_record_p->recdes.data + catalog_record_p->offset);
  catalog_record_p->offset += CATALOG_DISK_ATTR_SIZE;

  return NO_ERROR;
}

/*
 * catalog_fetch_attribute_value () -
 *   return: NO_ERROR or ER_FAILED
 *   value(in): pointer to the value data (disk representation)
 *   length(in): length of the value data
 *   ct_recordp(in): pointer to CATALOG_RECORD structure (catalog record)
 *
 * Note: Transforms catalog disk form into disk representation form.
 * Fetch value data from catalog record.
 */
static int
catalog_fetch_attribute_value (THREAD_ENTRY * thread_p, void *value, int length, CATALOG_RECORD * catalog_record_p)
{
  int offset = 0;
  int bufsize;

  if (catalog_read_unread_portion (thread_p, catalog_record_p, length) != NO_ERROR)
    {
      return ER_FAILED;
    }

  while (offset < length)
    {
      if (length - offset <= catalog_record_p->recdes.length - catalog_record_p->offset)
	{
	  /* if the size of the value is smaller than or equals to the remaining length of the recdes.data, just read
	   * the value from the recdes.data buffer and adjust the offset. */
	  bufsize = length - offset;
	  (void) memcpy ((char *) value + offset, catalog_record_p->recdes.data + catalog_record_p->offset, bufsize);
	  catalog_record_p->offset += bufsize;
	  break;
	}
      else
	{
	  /* if the size of the value is larger than the whole length of the recdes.data, that means the value has been 
	   * stored in N pages, we need to fetch these N pages and read value from them. in first N-1 page, the whole
	   * page will be read into the value buffer, while in last page, the remaining value will be read into value
	   * buffer as the existing routine. */
	  assert (catalog_record_p->offset == 0);
	  bufsize = catalog_record_p->recdes.length;
	  (void) memcpy ((char *) value + offset, catalog_record_p->recdes.data, bufsize);
	  offset += bufsize;

	  /* read next page and fill the catalog_record_p */
	  catalog_record_p->offset = catalog_record_p->recdes.length;
	  if (catalog_get_record_from_page (thread_p, catalog_record_p) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * catalog_fetch_btree_statistics () -
 *   return: NO_ERROR or ER_FAILED
 *   bt_statsp(in): pointer to BTREE_STATS structure (disk representation)
 *   ct_recordp(in): pointer to CATALOG_RECORD structure (catalog record)
 *
 * Note: Transforms catalog disk form into disk representation form.
 * Fetch BTREE_STATS structure from catalog record.
 */
static int
catalog_fetch_btree_statistics (THREAD_ENTRY * thread_p, BTREE_STATS * btree_stats_p, CATALOG_RECORD * catalog_record_p)
{
  VPID root_vpid;
  PAGE_PTR root_page_p;
  BTREE_ROOT_HEADER *root_header = NULL;
  int i;
  OR_BUF buf;

  if (catalog_read_unread_portion (thread_p, catalog_record_p, CATALOG_BT_STATS_SIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  btree_stats_p->pkeys_size = 0;
  btree_stats_p->pkeys = NULL;

  CATALOG_GET_BT_STATS_BTID (&btree_stats_p->btid, catalog_record_p->recdes.data + catalog_record_p->offset);

  root_vpid.pageid = btree_stats_p->btid.root_pageid;
  root_vpid.volid = btree_stats_p->btid.vfid.volid;
  if (VPID_ISNULL (&root_vpid))
    {
      /* after create the catalog record of the class, and before create the catalog record of the constraints for the
       * class currently, does not know BTID */
      btree_stats_p->key_type = &tp_Null_domain;
      goto exit_on_end;
    }

  root_page_p = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (root_page_p == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, root_page_p, PAGE_BTREE);

  root_header = btree_get_root_header (root_page_p);
  if (root_header == NULL)
    {
      pgbuf_unfix_and_init (thread_p, root_page_p);
      return ER_FAILED;
    }

  or_init (&buf, root_header->packed_key_domain, -1);
  btree_stats_p->key_type = or_get_domain (&buf, NULL, NULL);

  pgbuf_unfix_and_init (thread_p, root_page_p);

  if (TP_DOMAIN_TYPE (btree_stats_p->key_type) == DB_TYPE_MIDXKEY)
    {
      btree_stats_p->pkeys_size = tp_domain_size (btree_stats_p->key_type->setdomain);
    }
  else
    {
      btree_stats_p->pkeys_size = 1;
    }

  /* cut-off to stats */
  if (btree_stats_p->pkeys_size > BTREE_STATS_PKEYS_NUM)
    {
      btree_stats_p->pkeys_size = BTREE_STATS_PKEYS_NUM;
    }

  btree_stats_p->pkeys = (int *) db_private_alloc (thread_p, btree_stats_p->pkeys_size * sizeof (int));
  if (btree_stats_p->pkeys == NULL)
    {
      return ER_FAILED;
    }

  assert (btree_stats_p->pkeys_size <= BTREE_STATS_PKEYS_NUM);
  for (i = 0; i < btree_stats_p->pkeys_size; i++)
    {
      btree_stats_p->pkeys[i] = 0;
    }

exit_on_end:

  catalog_get_btree_statistics (btree_stats_p, catalog_record_p->recdes.data + catalog_record_p->offset);
  catalog_record_p->offset += CATALOG_BT_STATS_SIZE;

  return NO_ERROR;
}

static int
catalog_drop_representation_helper (THREAD_ENTRY * thread_p, PAGE_PTR page_p, VPID * page_id_p, PGSLOTID slot_id)
{
  PAGE_PTR overflow_page_p;
  VPID overflow_vpid, new_overflow_vpid;
  PGLENGTH new_space;
  RECDES record = { 0, -1, REC_HOME, NULL };

  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (spage_get_record (page_p, slot_id, &record, COPY) != S_SUCCESS)
    {
      recdes_free_data_area (&record);
      if (er_errid () == ER_SP_UNKNOWN_SLOTID)
	{
	  return NO_ERROR;
	}
      return ER_FAILED;
    }

  log_append_undoredo_recdes2 (thread_p, RVCT_DELETE, &catalog_Id.vfid, page_p, slot_id, &record, NULL);

  if (spage_delete (thread_p, page_p, slot_id) != slot_id)
    {
      recdes_free_data_area (&record);
      return ER_FAILED;
    }

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  catalog_update_max_space (page_id_p, new_space);

  spage_get_record (page_p, CATALOG_HEADER_SLOT, &record, COPY);

  log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, CATALOG_HEADER_SLOT, &record);

  overflow_vpid.pageid = CATALOG_GET_PGHEADER_OVFL_PGID_PAGEID (record.data);
  overflow_vpid.volid = CATALOG_GET_PGHEADER_OVFL_PGID_VOLID (record.data);

  if (overflow_vpid.pageid != NULL_PAGEID)
    {
      CATALOG_PUT_PGHEADER_OVFL_PGID_PAGEID (record.data, NULL_PAGEID);
      CATALOG_PUT_PGHEADER_OVFL_PGID_VOLID (record.data, NULL_VOLID);

      if (spage_update (thread_p, page_p, CATALOG_HEADER_SLOT, &record) != SP_SUCCESS)
	{
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}

      log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, CATALOG_HEADER_SLOT, &record);
    }

  recdes_free_data_area (&record);

  while (overflow_vpid.pageid != NULL_PAGEID)
    {
      /* delete the records in the overflow pages, if any */
      overflow_page_p = pgbuf_fix (thread_p, &overflow_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (overflow_page_p == NULL)
	{
	  return ER_FAILED;
	}

      (void) pgbuf_check_page_ptype (thread_p, overflow_page_p, PAGE_CATALOG);

      spage_get_record (overflow_page_p, CATALOG_HEADER_SLOT, &record, PEEK);
      new_overflow_vpid.pageid = CATALOG_GET_PGHEADER_OVFL_PGID_PAGEID (record.data);
      new_overflow_vpid.volid = CATALOG_GET_PGHEADER_OVFL_PGID_VOLID (record.data);

      pgbuf_unfix_and_init (thread_p, overflow_page_p);
      if (file_dealloc (thread_p, &catalog_Id.vfid, &overflow_vpid, FILE_CATALOG) != NO_ERROR)
	{
	  assert (false);
	}
      overflow_vpid = new_overflow_vpid;
    }

  return NO_ERROR;
}

/*
 * catalog_drop_disk_representation_from_page () -
 *   return: NO_ERROR or ER_FAILED
 *   page_id(in): Page identifier for the catalog unit
 *   slot_id(in): Slot identifier for the catalog unit
 *
 * Note: The catalog storage unit whose location is identified with
 * the given page and slot identifier is deleted from the
 * catalog page. If there overflow pages pointed by the given
 * catalog unit, the overflow pages are deallocated.
 */
static int
catalog_drop_disk_representation_from_page (THREAD_ENTRY * thread_p, VPID * page_id_p, PGSLOTID slot_id)
{
  PAGE_PTR page_p;

  page_p = pgbuf_fix (thread_p, page_id_p, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  if (catalog_drop_representation_helper (thread_p, page_p, page_id_p, slot_id) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      return ER_FAILED;
    }

  pgbuf_set_dirty (thread_p, page_p, FREE);
  return NO_ERROR;
}

/*
 * catalog_drop_representation_class_from_page () -
 *   return: NO_ERROR or ER_FAILED
 *   dir_pgid(in): Directory page identifier
 *   dir_pgptr(in/out): Directory page pointer
 *   page_id(in): Catalog unit page identifier
 *   slot_id(in): Catalog unit slot identifier
 *
 * Note: The catalog storage unit which can be disk representation or
 * class information record, whose location is identified by the
 * given page and slot identifier is deleted from the catalog page.
 */
static int
catalog_drop_representation_class_from_page (THREAD_ENTRY * thread_p, VPID * dir_page_id_p, PAGE_PTR * dir_page_p,
					     VPID * page_id_p, PGSLOTID slot_id)
{
  PAGE_PTR page_p = NULL;
  bool same_page;

  assert (dir_page_p != NULL && (*dir_page_p) != NULL);
  /* directory and repr. to be deleted are on the same page? */
  same_page = VPID_EQ (page_id_p, dir_page_id_p) ? true : false;

  if (same_page)
    {
      page_p = *dir_page_p;
    }
  else
    {
      int again_count = 0;
      int again_max = 20;

    try_again:
      /* avoid page deadlock */
      page_p = pgbuf_fix (thread_p, page_id_p, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH);
      if (page_p == NULL)
	{
	  /* try reverse order */
	  pgbuf_unfix_and_init (thread_p, *dir_page_p);

	  page_p = pgbuf_fix (thread_p, page_id_p, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (page_p == NULL)
	    {
	      return ER_FAILED;
	    }

	  *dir_page_p = pgbuf_fix (thread_p, dir_page_id_p, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH);
	  if ((*dir_page_p) == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, page_p);

	      *dir_page_p = pgbuf_fix (thread_p, dir_page_id_p, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if ((*dir_page_p) == NULL)
		{
		  return ER_FAILED;
		}

	      if (again_count++ >= again_max)
		{
		  if (er_errid () == NO_ERROR)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PAGE_LATCH_ABORTED, 2, page_id_p->volid,
			      page_id_p->pageid);
		    }

		  return ER_FAILED;
		}
	      else
		{
		  goto try_again;
		}
	    }
	}

      (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);
    }

  if (catalog_drop_representation_helper (thread_p, page_p, page_id_p, slot_id) != NO_ERROR)
    {
      if (!same_page)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	}
      return ER_FAILED;
    }

  if (same_page)
    {
      pgbuf_set_dirty (thread_p, page_p, DONT_FREE);
    }
  else
    {
      pgbuf_set_dirty (thread_p, page_p, FREE);
    }

  return NO_ERROR;
}

static int
catalog_get_rep_dir (THREAD_ENTRY * thread_p, OID * class_oid_p, OID * rep_dir_p, bool lookup_hash)
{
  CATALOG_REPR_ITEM repr_item = CATALOG_REPR_ITEM_INITIALIZER;
  PAGE_PTR page_p;
  CLS_INFO class_info = CLS_INFO_INITIALIZER;

  HEAP_SCANCACHE scan_cache;
  RECDES record = { -1, -1, REC_HOME, NULL };
  int error_code = NO_ERROR;

  assert (class_oid_p != NULL);
  assert (!OID_ISNULL (class_oid_p));
  assert (rep_dir_p != NULL);
  assert (OID_ISNULL (rep_dir_p));

  /* 1st try: look up class_info record in Catalog hash */

  assert (repr_item.repr_id == NULL_REPRID);
  repr_item.repr_id = NULL_REPRID;
  if (lookup_hash == true && catalog_get_representation_item (thread_p, class_oid_p, &repr_item) == NO_ERROR
      && !VPID_ISNULL (&(repr_item.page_id)))
    {
      page_p = pgbuf_fix (thread_p, &repr_item.page_id, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (page_p == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();

	  return error_code;
	}

      (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

      if (spage_get_record (page_p, repr_item.slot_id, &record, PEEK) != S_SUCCESS)
	{
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();

	  pgbuf_unfix_and_init (thread_p, page_p);

	  return error_code;
	}

      catalog_get_class_info_from_record (&class_info, record.data);

      pgbuf_unfix_and_init (thread_p, page_p);

      assert (!OID_ISNULL (&(class_info.ci_rep_dir)));
      COPY_OID (rep_dir_p, &(class_info.ci_rep_dir));
    }

  if (OID_ISNULL (rep_dir_p))
    {
      /* 2nd try: look up class record in Rootclass */

      heap_scancache_quick_start_root_hfid (thread_p, &scan_cache);

      if (heap_get_class_record (thread_p, class_oid_p, &record, &scan_cache, PEEK) == S_SUCCESS)
	{
	  or_class_rep_dir (&record, rep_dir_p);
	}

      heap_scancache_end (thread_p, &scan_cache);
    }

  if (OID_ISNULL (rep_dir_p))
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();

      return error_code;
    }

  assert (error_code == NO_ERROR);

  return error_code;
}

static PAGE_PTR
catalog_get_representation_record (THREAD_ENTRY * thread_p, OID * rep_dir_p, RECDES * record_p, PGBUF_LATCH_MODE latch,
				   int is_peek, int *out_repr_count_p)
{
  PAGE_PTR page_p;
  VPID vpid;

  assert (rep_dir_p != NULL);
  assert (!OID_ISNULL (rep_dir_p));

  vpid.volid = rep_dir_p->volid;
  vpid.pageid = rep_dir_p->pageid;

  page_p = pgbuf_fix (thread_p, &vpid, OLD_PAGE, latch, PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, rep_dir_p->volid, rep_dir_p->pageid,
		  rep_dir_p->slotid);
	}
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  if (spage_get_record (page_p, rep_dir_p->slotid, record_p, is_peek) != S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      return NULL;
    }

  assert (record_p->length == CATALOG_REPR_ITEM_SIZE * 2);

  *out_repr_count_p = CATALOG_GET_REPR_ITEM_COUNT (record_p->data);
  assert (*out_repr_count_p == 1 || *out_repr_count_p == 2);

  return page_p;
}

static PAGE_PTR
catalog_get_representation_record_after_search (THREAD_ENTRY * thread_p, OID * class_id_p, RECDES * record_p,
						PGBUF_LATCH_MODE latch, int is_peek, OID * rep_dir_p,
						int *out_repr_count_p, bool lookup_hash)
{
  assert (class_id_p != NULL);
  assert (!OID_ISNULL (class_id_p));
  assert (rep_dir_p != NULL);
  assert (OID_ISNULL (rep_dir_p));

  /* get old directory for the class */
  if (catalog_get_rep_dir (thread_p, class_id_p, rep_dir_p, lookup_hash) != NO_ERROR || OID_ISNULL (rep_dir_p))
    {
      assert (er_errid () != NO_ERROR);
      return NULL;
    }

  return catalog_get_representation_record (thread_p, rep_dir_p, record_p, latch, is_peek, out_repr_count_p);
}

static int
catalog_adjust_directory_count (THREAD_ENTRY * thread_p, PAGE_PTR page_p, RECDES * record_p, int delta)
{
  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  spage_get_record (page_p, CATALOG_HEADER_SLOT, record_p, COPY);

  log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, CATALOG_HEADER_SLOT, record_p);

  CATALOG_PUT_PGHEADER_DIR_COUNT (record_p->data, CATALOG_GET_PGHEADER_DIR_COUNT (record_p->data) + delta);

  if (spage_update (thread_p, page_p, CATALOG_HEADER_SLOT, record_p) != SP_SUCCESS)
    {
      return ER_FAILED;
    }

  log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, CATALOG_HEADER_SLOT, record_p);

  return NO_ERROR;
}

static void
catalog_delete_key (OID * class_id_p, REPR_ID repr_id)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (NULL, THREAD_TS_CATALOG);
  CATALOG_KEY catalog_key;

  catalog_key.page_id = class_id_p->pageid;
  catalog_key.volid = class_id_p->volid;
  catalog_key.slot_id = class_id_p->slotid;
  catalog_key.repr_id = repr_id;

  if (lf_hash_delete (t_entry, &catalog_Hash_table, (void *) &catalog_key, NULL) != NO_ERROR)
    {
      assert (false);
    }
}

static char *
catalog_find_representation_item_position (INT16 repr_id, int repr_cnt, char *repr_p, int *out_position_p)
{
  int position = 0;

  while (position < repr_cnt && repr_id != CATALOG_GET_REPR_ITEM_REPRID (repr_p))
    {
      position++;
      repr_p += CATALOG_REPR_ITEM_SIZE;
    }

  *out_position_p = position;
  assert (*out_position_p <= 2);	/* class info repr, last repr */

  return repr_p;
}

static int
catalog_insert_representation_item (THREAD_ENTRY * thread_p, RECDES * record_p, OID * rep_dir_p)
{
  PAGE_PTR page_p;
  VPID page_id;
  PGSLOTID slot_id;
  PGLENGTH new_space;

  assert (record_p != NULL);
  assert (OR_GET_BYTE (record_p->data + CATALOG_REPR_ITEM_COUNT_OFF) == 1);
  assert (record_p->length == CATALOG_REPR_ITEM_SIZE * 2);

  assert (rep_dir_p != NULL);
  assert (OID_ISNULL (rep_dir_p));

  page_p = catalog_find_optimal_page (thread_p, record_p->length, &page_id);
  if (page_p == NULL)
    {
      return ER_FAILED;
    }

  if (spage_insert (thread_p, page_p, record_p, &slot_id) != SP_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      return ER_FAILED;
    }

  log_append_undoredo_recdes2 (thread_p, RVCT_INSERT, &catalog_Id.vfid, page_p, slot_id, NULL, record_p);

  if (catalog_adjust_directory_count (thread_p, page_p, record_p, 1) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      return ER_FAILED;
    }

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  pgbuf_set_dirty (thread_p, page_p, FREE);
  catalog_update_max_space (&page_id, new_space);

  rep_dir_p->volid = page_id.volid;
  rep_dir_p->pageid = page_id.pageid;
  rep_dir_p->slotid = slot_id;

  if (OID_ISNULL (rep_dir_p))
    {
      assert (false);		/* is impossible */

      return ER_FAILED;
    }

  return NO_ERROR;
}


/*
 * catalog_put_representation_item () -
 *   return: NO_ERROR or ER_FAILED
 *   class_id_p(in): Class object identifier
 *   repr_item_p(in): Representation Item
 *   rep_dir_p(in/out): Representation Directory
 *
 * Note: The given representation item is inserted to the class
 * directory, if any. If there is no class directory, one is
 * created to contain the given item.
 * If there is already a class directory, the given
 * item replaces the old representation item with the same
 * representation identifier, if any, otherwise it is added to
 * the directory.
 */
static int
catalog_put_representation_item (THREAD_ENTRY * thread_p, OID * class_id_p, CATALOG_REPR_ITEM * repr_item_p,
				 OID * rep_dir_p)
{
  PAGE_PTR page_p;
  VPID page_id;
  PGSLOTID slot_id;
#if 0				/* TODO - dead code; do not delete me for future use */
  PGLENGTH new_space;
  char page_header_data[CATALOG_PAGE_HEADER_SIZE + MAX_ALIGNMENT];
  char *aligned_page_header_data;
#endif
  RECDES record = { 0, -1, REC_HOME, NULL };
  RECDES tmp_record = { 0, -1, REC_HOME, NULL };
  int repr_pos, repr_count;
  char *repr_p;
  int success;

  assert (rep_dir_p != NULL);

#if 0				/* TODO - dead code; do not delete me for future use */
  aligned_page_header_data = PTR_ALIGN (page_header_data, MAX_ALIGNMENT);
#endif

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* add new directory for the class */
  if (OID_ISNULL (rep_dir_p))
    {
      record.length = CATALOG_REPR_ITEM_SIZE * 2;
      catalog_put_repr_item_to_record (record.data, repr_item_p);

      /* save #repr */
      OR_PUT_BYTE (record.data + CATALOG_REPR_ITEM_COUNT_OFF, 1);
      assert (record.length == CATALOG_REPR_ITEM_SIZE * 2);

      if (catalog_insert_representation_item (thread_p, &record, rep_dir_p) != NO_ERROR || OID_ISNULL (rep_dir_p))
	{
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}

      assert (!OID_ISNULL (rep_dir_p));
    }
  else
    {
      /* get old directory for the class */
      page_id.volid = rep_dir_p->volid;
      page_id.pageid = rep_dir_p->pageid;

      page_p = catalog_get_representation_record (thread_p, rep_dir_p, &record, PGBUF_LATCH_WRITE, COPY, &repr_count);
      if (page_p == NULL)
	{
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}

      repr_p = catalog_find_representation_item_position (repr_item_p->repr_id, repr_count, record.data, &repr_pos);

      if (repr_pos < repr_count)
	{
	  log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, rep_dir_p->slotid, &record);

	  page_id.pageid = CATALOG_GET_REPR_ITEM_PAGEID_PAGEID (repr_p);
	  page_id.volid = CATALOG_GET_REPR_ITEM_PAGEID_VOLID (repr_p);
	  slot_id = CATALOG_GET_REPR_ITEM_SLOTID (repr_p);

	  catalog_delete_key (class_id_p, repr_item_p->repr_id);
	  catalog_put_repr_item_to_record (repr_p, repr_item_p);

	  /* save #repr */
	  OR_PUT_BYTE (record.data + CATALOG_REPR_ITEM_COUNT_OFF, repr_count);
	  assert (record.length == CATALOG_REPR_ITEM_SIZE * 2);

	  if (spage_update (thread_p, page_p, rep_dir_p->slotid, &record) != SP_SUCCESS)
	    {
	      recdes_free_data_area (&record);
	      pgbuf_unfix_and_init (thread_p, page_p);
	      return ER_FAILED;
	    }

	  log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, rep_dir_p->slotid, &record);
	  pgbuf_set_dirty (thread_p, page_p, FREE);

	  /* delete the old representation */
	  if (catalog_drop_disk_representation_from_page (thread_p, &page_id, slot_id) != NO_ERROR)
	    {
	      recdes_free_data_area (&record);
	      return ER_FAILED;
	    }
	}
      else			/* a new representation identifier */
	{
	  assert (repr_count == 1);

	  /* copy old directory for logging purposes */

	  if (recdes_allocate_data_area (&tmp_record, record.length) != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, page_p);
	      recdes_free_data_area (&record);
	      return ER_FAILED;
	    }

	  tmp_record.length = record.length;
	  tmp_record.type = record.type;
	  memcpy (tmp_record.data, record.data, record.length);

	  /* add the new representation item */
	  catalog_put_repr_item_to_record (record.data + CATALOG_REPR_ITEM_SIZE, repr_item_p);
	  /* save #repr */
	  OR_PUT_BYTE (record.data + CATALOG_REPR_ITEM_COUNT_OFF, repr_count + 1);
	  assert (OR_GET_BYTE (record.data + CATALOG_REPR_ITEM_COUNT_OFF) == 2);
	  assert (record.length == CATALOG_REPR_ITEM_SIZE * 2);

	  log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, rep_dir_p->slotid, &tmp_record);

	  success = spage_update (thread_p, page_p, rep_dir_p->slotid, &record);
	  if (success == SP_SUCCESS)
	    {
	      recdes_free_data_area (&tmp_record);
	      log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, rep_dir_p->slotid, &record);
	      pgbuf_set_dirty (thread_p, page_p, FREE);
	    }
#if 0				/* TODO - dead code; do not delete me for future use */
	  else if (success == SP_DOESNT_FIT)
	    {
	      assert (false);	/* is impossible */

	      /* the directory needs to be deleted from the current page and moved to another page. */

	      ehash_delete (thread_p, &catalog_Id.xhid, key);
	      log_append_undoredo_recdes2 (thread_p, RVCT_DELETE, &catalog_Id.vfid, page_p, rep_dir_p->slotid,
					   &tmp_record, NULL);
	      recdes_free_data_area (&tmp_record);

	      spage_delete (thread_p, page_p, rep_dir_p->slotid);
	      new_space = spage_max_space_for_new_record (thread_p, page_p);

	      recdes_set_data_area (&tmp_record, aligned_page_header_data, CATALOG_PAGE_HEADER_SIZE);

	      if (catalog_adjust_directory_count (thread_p, page_p, &tmp_record, -1) != NO_ERROR)
		{
		  pgbuf_unfix_and_init (thread_p, page_p);
		  recdes_free_data_area (&record);
		  return ER_FAILED;
		}

	      pgbuf_set_dirty (thread_p, page_p, FREE);
	      catalog_update_max_space (&page_id, new_space);

	      if (catalog_insert_representation_item (thread_p, &record, rep_dir_p) != NO_ERROR)
		{
		  recdes_free_data_area (&record);
		  return ER_FAILED;
		}
	    }
#endif
	  else
	    {
	      assert (false);	/* is impossible */

	      pgbuf_unfix_and_init (thread_p, page_p);
	      recdes_free_data_area (&tmp_record);
	      recdes_free_data_area (&record);
	      return ER_FAILED;
	    }
	}
    }

  recdes_free_data_area (&record);

  return NO_ERROR;
}

/*
 * catalog_get_representation_item () -
 *   return: NO_ERROR or ER_FAILED
 *   class_id(in): Class object identifier
 *   repr_item(in/out): Set to the Representation Item
 *                      (repr_item->repr_id must be set by the caller)
 *
 * Note: The representation item for the given class and the specified
 * representation identifier (set by the caller in repr_item) is
 * extracted from the class directory. This information tells
 * the caller where, in the catalog, the specifed represenation unit resides.
 */
static int
catalog_get_representation_item (THREAD_ENTRY * thread_p, OID * class_id_p, CATALOG_REPR_ITEM * repr_item_p)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_CATALOG);
  PAGE_PTR page_p;
  RECDES record;
  OID rep_dir;
  CATALOG_ENTRY *catalog_value_p;
  int repr_pos, repr_count;
  char *repr_p;
  CATALOG_KEY catalog_key;

  OID_SET_NULL (&rep_dir);	/* init */

  catalog_key.page_id = class_id_p->pageid;
  catalog_key.volid = class_id_p->volid;
  catalog_key.slot_id = class_id_p->slotid;
  catalog_key.repr_id = repr_item_p->repr_id;

  if (lf_hash_find (t_entry, &catalog_Hash_table, (void *) &catalog_key, (void **) &catalog_value_p) != NO_ERROR)
    {
      return ER_FAILED;
    }
  else if (catalog_value_p != NULL)
    {
      /* entry already exists */
      repr_item_p->page_id.volid = catalog_value_p->key.r_page_id.volid;
      repr_item_p->page_id.pageid = catalog_value_p->key.r_page_id.pageid;
      repr_item_p->slot_id = catalog_value_p->key.r_slot_id;

      /* end transaction */
      lf_tran_end_with_mb (t_entry);
      return NO_ERROR;
    }
  else
    {
      /* fresh entry, fetch data for it */

      assert (OID_ISNULL (&rep_dir));

      page_p =
	catalog_get_representation_record_after_search (thread_p, class_id_p, &record, PGBUF_LATCH_READ, PEEK, &rep_dir,
							&repr_count, false /* lookup_hash */ );
      if (page_p == NULL)
	{
	  return ER_FAILED;
	}

      repr_p = catalog_find_representation_item_position (repr_item_p->repr_id, repr_count, record.data, &repr_pos);

      if (repr_pos == repr_count)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_UNKNOWN_REPRID, 1, repr_item_p->repr_id);
	  return ER_FAILED;
	}

      repr_item_p->page_id.pageid = CATALOG_GET_REPR_ITEM_PAGEID_PAGEID (repr_p);
      repr_item_p->page_id.volid = CATALOG_GET_REPR_ITEM_PAGEID_VOLID (repr_p);
      repr_item_p->slot_id = CATALOG_GET_REPR_ITEM_SLOTID (repr_p);
      pgbuf_unfix_and_init (thread_p, page_p);

      /* set entry info for further use */
      catalog_key.r_page_id.pageid = repr_item_p->page_id.pageid;
      catalog_key.r_page_id.volid = repr_item_p->page_id.volid;
      catalog_key.r_slot_id = repr_item_p->slot_id;

      /* insert value */
      if (lf_hash_find_or_insert (t_entry, &catalog_Hash_table, (void *) &catalog_key, (void **) &catalog_value_p, NULL)
	  != NO_ERROR)
	{
	  return ER_FAILED;
	}
      else if (catalog_value_p != NULL)
	{
	  lf_tran_end_with_mb (t_entry);
	  return NO_ERROR;
	}
      else
	{
	  /* impossible case */
#if defined(CT_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"catalog_get_representation_item: Insertion to hash table failed.\n "
			"Key: Class_Id: { %d , %d , %d } Repr: %d", class_id_p->pageid, class_id_p->volid,
			class_id_p->slotid, repr_item_p->repr_id);
#endif /* CT_DEBUG */

	  return ER_FAILED;
	}
    }
}

static int
catalog_drop_directory (THREAD_ENTRY * thread_p, PAGE_PTR page_p, RECDES * record_p, OID * oid_p, OID * class_id_p)
{
  log_append_undoredo_recdes2 (thread_p, RVCT_DELETE, &catalog_Id.vfid, page_p, oid_p->slotid, record_p, NULL);

  if (spage_delete (thread_p, page_p, oid_p->slotid) != oid_p->slotid)
    {
      return ER_FAILED;
    }

  if (catalog_adjust_directory_count (thread_p, page_p, record_p, -1) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * catalog_drop_representation_item () -
 *   return: NO_ERROR or ER_FAILED
 *   class_id(in): Class object identifier
 *   repr_item(in): Representation Item
 *                  (repr_item->repr_id must be set by the caller)
 *
 * Note: The representation item for the given class and the specified
 * representation identifier (set by the caller in repr_item) is
 * dropped from the class directory.
 */
static int
catalog_drop_representation_item (THREAD_ENTRY * thread_p, OID * class_id_p, CATALOG_REPR_ITEM * repr_item_p)
{
  PAGE_PTR page_p;
  PGLENGTH new_space;
  char *repr_p, *next_p;
  int repr_pos, repr_count;
  RECDES record;
  OID rep_dir;
  VPID vpid;
  CATALOG_REPR_ITEM tmp_repr_item = CATALOG_REPR_ITEM_INITIALIZER;

  OID_SET_NULL (&rep_dir);	/* init */

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  page_p =
    catalog_get_representation_record_after_search (thread_p, class_id_p, &record, PGBUF_LATCH_WRITE, COPY, &rep_dir,
						    &repr_count, true /* lookup_hash */ );
  if (page_p == NULL)
    {
      recdes_free_data_area (&record);
      return ER_FAILED;
    }

  repr_p = catalog_find_representation_item_position (repr_item_p->repr_id, repr_count, record.data, &repr_pos);

  if (repr_pos >= repr_count)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      recdes_free_data_area (&record);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_UNKNOWN_REPRID, 1, repr_item_p->repr_id);
      return ER_FAILED;
    }

  repr_item_p->page_id.pageid = CATALOG_GET_REPR_ITEM_PAGEID_PAGEID (repr_p);
  repr_item_p->page_id.volid = CATALOG_GET_REPR_ITEM_PAGEID_VOLID (repr_p);
  repr_item_p->slot_id = CATALOG_GET_REPR_ITEM_SLOTID (repr_p);

  catalog_delete_key (class_id_p, repr_item_p->repr_id);

  if (repr_count > 1)
    {
      assert (repr_count == 2);

      /* the directory will be updated */

      log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, rep_dir.slotid, &record);

      next_p = repr_p;
      for (next_p += CATALOG_REPR_ITEM_SIZE; repr_pos < (repr_count - 1);
	   repr_pos++, repr_p += CATALOG_REPR_ITEM_SIZE, next_p += CATALOG_REPR_ITEM_SIZE)
	{
	  catalog_get_repr_item_from_record (&tmp_repr_item, next_p);
	  catalog_put_repr_item_to_record (repr_p, &tmp_repr_item);
	}

      /* save #repr */
      OR_PUT_BYTE (record.data + CATALOG_REPR_ITEM_COUNT_OFF, repr_count - 1);
      assert (OR_GET_BYTE (record.data + CATALOG_REPR_ITEM_COUNT_OFF) == 1);

      assert (record.length == CATALOG_REPR_ITEM_SIZE * 2);

      if (spage_update (thread_p, page_p, rep_dir.slotid, &record) != SP_SUCCESS)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}

      log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, rep_dir.slotid, &record);
    }
  else
    {
      if (catalog_drop_directory (thread_p, page_p, &record, &rep_dir, class_id_p) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}
    }

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  pgbuf_set_dirty (thread_p, page_p, FREE);

  vpid.volid = rep_dir.volid;
  vpid.pageid = rep_dir.pageid;
  catalog_update_max_space (&vpid, new_space);

  recdes_free_data_area (&record);
  return NO_ERROR;
}

static void
catalog_copy_btree_statistic (BTREE_STATS * new_btree_stats_p, int new_btree_stats_count,
			      BTREE_STATS * pre_btree_stats_p, int pre_btree_stats_count)
{
  BTREE_STATS *pre_stats_p, *new_stats_p;
  int i, j, k;

  for (i = 0, new_stats_p = new_btree_stats_p; i < new_btree_stats_count; i++, new_stats_p++)
    {
      for (j = 0, pre_stats_p = pre_btree_stats_p; j < pre_btree_stats_count; j++, pre_stats_p++)
	{
	  if (!BTID_IS_EQUAL (&new_stats_p->btid, &pre_stats_p->btid))
	    {
	      continue;
	    }

	  new_stats_p->btid = pre_stats_p->btid;
	  new_stats_p->leafs = pre_stats_p->leafs;
	  new_stats_p->pages = pre_stats_p->pages;
	  new_stats_p->height = pre_stats_p->height;
	  new_stats_p->keys = pre_stats_p->keys;
	  new_stats_p->key_type = pre_stats_p->key_type;
	  new_stats_p->pkeys_size = pre_stats_p->pkeys_size;

	  assert (new_stats_p->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	  for (k = 0; k < new_stats_p->pkeys_size; k++)
	    {
	      new_stats_p->pkeys[k] = pre_stats_p->pkeys[k];
	    }
#if 0				/* reserved for future use */
	  for (k = 0; k < BTREE_STATS_RESERVED_NUM; k++)
	    {
	      new_stats_p->reserved[k] = pre_stats_p->reserved[k];
	    }
#endif

	  break;
	}
    }
}

static void
catalog_copy_disk_attributes (DISK_ATTR * new_attrs_p, int new_attr_count, DISK_ATTR * pre_attrs_p, int pre_attr_count)
{
  DISK_ATTR *pre_attr_p, *new_attr_p;
  int i, j;

  for (i = 0, new_attr_p = new_attrs_p; i < new_attr_count; i++, new_attr_p++)
    {
      for (j = 0, pre_attr_p = pre_attrs_p; j < pre_attr_count; j++, pre_attr_p++)
	{
	  if (new_attr_p->id != pre_attr_p->id)
	    {
	      continue;
	    }

	  catalog_copy_btree_statistic (new_attr_p->bt_stats, new_attr_p->n_btstats, pre_attr_p->bt_stats,
					pre_attr_p->n_btstats);
	}
    }
}

/*
 * Catalog interface routines
 */

/*
 * catalog_initialize () - Initialize data for further catalog operations
 *   return: nothing
 *   catid(in): Catalog identifier taken from the system page
 *
 * Note: Creates and initializes a main memory hash table that will be
 * used by catalog operations for fast access to catalog data.
 * The max_rec_size global variable which shows the maximum
 * available space on a catalog page with a header, is set by the
 * data in the catalog header. This routine should always be
 * called before any other catalog operations, except catalog creation.
 */
void
catalog_initialize (CTID * catalog_id_p)
{
  int ret;

  if (catalog_Hash_table.hash_size > 0)
    {
      lf_hash_destroy (&catalog_Hash_table);
    }

  VFID_COPY (&catalog_Id.xhid, &catalog_id_p->xhid);
  catalog_Id.xhid.pageid = catalog_id_p->xhid.pageid;
  catalog_Id.vfid.fileid = catalog_id_p->vfid.fileid;
  catalog_Id.vfid.volid = catalog_id_p->vfid.volid;
  catalog_Id.hpgid = catalog_id_p->hpgid;

  ret = lf_freelist_init (&catalog_Hash_freelist, 1, 100, &catalog_entry_Descriptor, &catalog_Ts);
  assert (ret == NO_ERROR);
  ret = lf_hash_init (&catalog_Hash_table, &catalog_Hash_freelist, CATALOG_HASH_SIZE, &catalog_entry_Descriptor);
  assert (ret == NO_ERROR);

  catalog_Max_record_size =
    spage_max_record_size () - CATALOG_PAGE_HEADER_SIZE - CATALOG_MAX_SLOT_ID_SIZE - CATALOG_MAX_SLOT_ID_SIZE;

  if (catalog_is_header_initialized == false)
    {
      catalog_initialize_max_space (&catalog_Max_space);
      catalog_is_header_initialized = true;
    }
}

/*
 * catalog_finalize () - Finalize the catalog operations by destroying the catalog
 *               memory hash table.
 *   return: nothing
 */
void
catalog_finalize (void)
{
  lf_hash_destroy (&catalog_Hash_table);
  lf_freelist_destroy (&catalog_Hash_freelist);
}

/*
 * catalog_create () -
 *   return: CTID * (catid on success and NULL on failure)
 *   catalog_id_p(out): Catalog identifier.
 *               All the fields in the identifier are set except the catalog
 *               and catalog index volume identifiers which should have been
 *               set by the caller.
 *
 * Note: Creates the catalog and an index that will be used for fast
 * catalog search. The index used is an extendible hashing index.
 * The first page (header page) of the catalog is allocated and
 * catalog header information is initialized.
 */
CTID *
catalog_create (THREAD_ENTRY * thread_p, CTID * catalog_id_p)
{
  PAGE_PTR page_p;
  VPID first_page_vpid;
  int new_space;
  bool is_overflow_page = false;

  log_sysop_start (thread_p);

  if (xehash_create (thread_p, &catalog_id_p->xhid, DB_TYPE_OBJECT, 1, oid_Root_class_oid, -1, false) == NULL)
    {
      ASSERT_ERROR ();
      goto error;
    }

  if (file_create_with_npages (thread_p, FILE_CATALOG, 1, NULL, &catalog_id_p->vfid) != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error;
    }

  if (file_alloc_sticky_first_page (thread_p, &catalog_id_p->vfid, catalog_initialize_new_page, &is_overflow_page,
				    &first_page_vpid, &page_p) != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error;
    }
  if (first_page_vpid.volid != catalog_id_p->vfid.volid)
    {
      assert_release (false);
      goto error;
    }
  if (page_p == NULL)
    {
      assert_release (false);
      goto error;
    }
  catalog_id_p->hpgid = first_page_vpid.pageid;

  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  if (catalog_is_header_initialized == false)
    {
      catalog_initialize_max_space (&catalog_Max_space);
      catalog_is_header_initialized = true;
    }

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  catalog_update_max_space (&first_page_vpid, new_space);
  pgbuf_unfix_and_init (thread_p, page_p);

  /* success */
  log_sysop_attach_to_outer (thread_p);

  return catalog_id_p;

error:
  log_sysop_abort (thread_p);

  return NULL;
}

#if defined (SA_MODE)
/*
 * catalog_file_map_is_empty () - FILE_MAP_PAGE_FUNC to check if catalog pages are empty. if true, it is save into a
 *                                collector.
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * page (in)     : catalog page pointer
 * stop (in)     : not used
 * args (in)     : CATALOG_PAGE_COLLECTOR *
 */
static int
catalog_file_map_is_empty (THREAD_ENTRY * thread_p, PAGE_PTR * page, bool * stop, void *args)
{
  CATALOG_PAGE_COLLECTOR *collector = (CATALOG_PAGE_COLLECTOR *) args;
  (void) pgbuf_check_page_ptype (thread_p, *page, PAGE_CATALOG);

  if (spage_number_of_records (*page) <= 1)
    {
      pgbuf_get_vpid (*page, &collector->vpids[collector->n_vpids++]);
    }
  return NO_ERROR;
}
#endif /* SA_MODE */

/*
 * catalog_reclaim_space () - Reclaim catalog space by deallocating all the empty
 *                       pages.
 *   return: NO_ERROR or ER_FAILED
 *
 * Note: This routine is supposed to be called only OFF-LINE.
 */
int
catalog_reclaim_space (THREAD_ENTRY * thread_p)
{
#if defined (SA_MODE)
  CATALOG_PAGE_COLLECTOR collector;
  int n_pages;
  int iter_vpid;

  int error_code = NO_ERROR;

  /* reinitialize catalog hinted page information, this is needed since the page pointed by this header may be
   * deallocated by this routine and the hinted page structure may have a dangling page pointer. */
  catalog_initialize_max_space (&catalog_Max_space);

  error_code = file_get_num_user_pages (thread_p, &catalog_Id.vfid, &n_pages);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  if (n_pages <= 0)
    {
      assert_release (false);
      return ER_FAILED;
    }

  collector.vpids = (VPID *) malloc (n_pages * sizeof (VPID));
  if (collector.vpids == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, n_pages * sizeof (VPID));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  collector.n_vpids = 0;

  error_code =
    file_map_pages (thread_p, &catalog_Id.vfid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, catalog_file_map_is_empty,
		    &collector);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      free (collector.vpids);
      return error_code;
    }

  for (iter_vpid = 0; iter_vpid < collector.n_vpids; iter_vpid++)
    {
      error_code = file_dealloc (thread_p, &catalog_Id.vfid, &collector.vpids[iter_vpid], FILE_CATALOG);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  free (collector.vpids);
	  return error_code;
	}
    }
  free (collector.vpids);
#endif /* SA_MODE */
  return NO_ERROR;
}

static int
catalog_sum_disk_attribute_size (DISK_ATTR * attrs_p, int count)
{
  int i, j, size = 0;
  DISK_ATTR *disk_attrp;

  for (i = 0, disk_attrp = attrs_p; i < count; i++, disk_attrp++)
    {
      size += CATALOG_DISK_ATTR_SIZE;
      size += disk_attrp->val_length + (MAX_ALIGNMENT * 2);
      for (j = 0; j < disk_attrp->n_btstats; j++)
	{
	  size += CATALOG_BT_STATS_SIZE;
	}
    }

  return size;
}

/*
 * catalog_add_representation () - Add disk representation to the catalog
 *   return: int
 *   class_id_p(in): Class identifier
 *   repr_id(in): Disk Representation identifier
 *   disk_repr_p(in): Pointer to the disk representation structure
 *   rep_dir_p(in/out): Representation Directory
 *   catalog_access_info_p(in): access info on catalog; if this is NULL
 *				locking of directory OID is performed here,
 *				otherwise the caller is reponsible for
 *				protecting concurrent access.*
 */
int
catalog_add_representation (THREAD_ENTRY * thread_p, OID * class_id_p, REPR_ID repr_id, DISK_REPR * disk_repr_p,
			    OID * rep_dir_p, CATALOG_ACCESS_INFO * catalog_access_info_p)
{
  CATALOG_REPR_ITEM repr_item = CATALOG_REPR_ITEM_INITIALIZER;
  CATALOG_RECORD catalog_record;
  PAGE_PTR page_p;
  PGLENGTH new_space;
  VPID vpid;
  DISK_ATTR *disk_attr_p;
  BTREE_STATS *btree_stats_p;
  int size, i, j;
  char *data;
  PGSLOTID remembered_slot_id = NULL_SLOTID;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;
  OID dir_oid;
  int error_code = NO_ERROR;
  bool do_end_access = false;

  assert (rep_dir_p != NULL);

  if (OID_ISTEMP (class_id_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_CLASSID, 3, class_id_p->volid, class_id_p->pageid,
	      class_id_p->slotid);
      return ER_CT_INVALID_CLASSID;
    }

  if (repr_id == NULL_REPRID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_REPRID, 1, repr_id);
      return ER_CT_INVALID_REPRID;
    }

  size = CATALOG_DISK_REPR_SIZE;
  size += catalog_sum_disk_attribute_size (disk_repr_p->fixed, disk_repr_p->n_fixed);
  size += catalog_sum_disk_attribute_size (disk_repr_p->variable, disk_repr_p->n_variable);

  if (catalog_access_info_p == NULL)
    {
      do_end_access = true;
      error_code = catalog_get_dir_oid_from_cache (thread_p, class_id_p, &dir_oid);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      catalog_access_info.class_oid = class_id_p;
      catalog_access_info.dir_oid = &dir_oid;
      error_code = catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, X_LOCK);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
      catalog_access_info_p = &catalog_access_info;
    }

  page_p = catalog_find_optimal_page (thread_p, size, &vpid);
  if (page_p == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return error_code;
    }

  repr_item.page_id.pageid = vpid.pageid;
  repr_item.page_id.volid = vpid.volid;
  repr_item.slot_id = NULL_SLOTID;
  repr_item.repr_id = repr_id;

  data = (char *) db_private_alloc (thread_p, DB_PAGESIZE);
  if (!data)
    {
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  catalog_record.vpid.pageid = repr_item.page_id.pageid;
  catalog_record.vpid.volid = repr_item.page_id.volid;
  catalog_record.slotid = NULL_SLOTID;
  catalog_record.page_p = page_p;
  catalog_record.recdes.area_size = spage_max_space_for_new_record (thread_p, page_p) - CATALOG_MAX_SLOT_ID_SIZE;
  catalog_record.recdes.length = 0;
  catalog_record.recdes.type = REC_HOME;
  catalog_record.recdes.data = data;
  catalog_record.offset = 0;

  if (catalog_store_disk_representation (thread_p, disk_repr_p, &catalog_record, &remembered_slot_id) != NO_ERROR)
    {
      db_private_free_and_init (thread_p, data);
      ASSERT_ERROR_AND_SET (error_code);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return error_code;
    }

  new_space = spage_max_space_for_new_record (thread_p, catalog_record.page_p);
  catalog_update_max_space (&repr_item.page_id, new_space);

  for (i = 0; i < disk_repr_p->n_fixed + disk_repr_p->n_variable; i++)
    {
      if (i < disk_repr_p->n_fixed)
	{
	  disk_attr_p = &disk_repr_p->fixed[i];
	}
      else
	{
	  disk_attr_p = &disk_repr_p->variable[i - disk_repr_p->n_fixed];
	}

      if (catalog_store_disk_attribute (thread_p, disk_attr_p, &catalog_record, &remembered_slot_id) != NO_ERROR)
	{
	  db_private_free_and_init (thread_p, data);

	  ASSERT_ERROR_AND_SET (error_code);
	  if (do_end_access)
	    {
	      catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	    }
	  return error_code;
	}

      if (catalog_store_attribute_value (thread_p, disk_attr_p->value, disk_attr_p->val_length, &catalog_record,
					 &remembered_slot_id) != NO_ERROR)
	{
	  db_private_free_and_init (thread_p, data);

	  ASSERT_ERROR_AND_SET (error_code);
	  if (do_end_access)
	    {
	      catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	    }
	  return error_code;
	}

      for (j = 0; j < disk_attr_p->n_btstats; j++)
	{
	  btree_stats_p = &disk_attr_p->bt_stats[j];

	  assert (btree_stats_p->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	  if (catalog_store_btree_statistics (thread_p, btree_stats_p, &catalog_record, &remembered_slot_id) !=
	      NO_ERROR)
	    {
	      db_private_free_and_init (thread_p, data);

	      ASSERT_ERROR_AND_SET (error_code);
	      if (do_end_access)
		{
		  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
		}
	      return error_code;
	    }
	}
    }

  catalog_record.recdes.length = catalog_record.offset;
  catalog_record.offset = catalog_record.recdes.area_size;

  if (catalog_put_record_into_page (thread_p, &catalog_record, 0, &remembered_slot_id) != NO_ERROR)
    {
      db_private_free_and_init (thread_p, data);

      ASSERT_ERROR_AND_SET (error_code);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return error_code;
    }

  pgbuf_unfix_and_init (thread_p, catalog_record.page_p);

  repr_item.slot_id = catalog_record.slotid;
  if (catalog_put_representation_item (thread_p, class_id_p, &repr_item, rep_dir_p) != NO_ERROR
      || OID_ISNULL (rep_dir_p))
    {
      db_private_free_and_init (thread_p, data);

      ASSERT_ERROR_AND_SET (error_code);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return error_code;
    }

  if (do_end_access)
    {
      catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, NO_ERROR);
    }

  db_private_free_and_init (thread_p, data);

  return NO_ERROR;
}

/*
 * catalog_add_class_info () - Add class information to the catalog
 *   return: int
 *   class_id_p(in): Class identifier
 *   class_info_p(in): Pointer to class specific information structure
 *   catalog_access_info_p(in): access info on catalog; if this is NULL
 *				locking of directory OID is performed here,
 *				otherwise the caller is reponsible for
 *				protecting concurrent access.
 */
int
catalog_add_class_info (THREAD_ENTRY * thread_p, OID * class_id_p, CLS_INFO * class_info_p,
			CATALOG_ACCESS_INFO * catalog_access_info_p)
{
  PAGE_PTR page_p;
  VPID page_id;
  PGLENGTH new_space;
  RECDES record = { -1, -1, REC_HOME, NULL };
  CATALOG_REPR_ITEM repr_item = CATALOG_REPR_ITEM_INITIALIZER;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;
  OID dir_oid;
  int success;
  int error_code;
  bool do_end_access = false;

  assert (class_info_p != NULL);
  assert (!OID_ISNULL (&(class_info_p->ci_rep_dir)));

  OID_SET_NULL (&dir_oid);

  if (OID_ISTEMP (class_id_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_CLASSID, 3, class_id_p->volid, class_id_p->pageid,
	      class_id_p->slotid);
      return ER_CT_INVALID_CLASSID;
    }

  assert (repr_item.repr_id == NULL_REPRID);

  if (catalog_access_info_p == NULL)
    {
      do_end_access = true;
      error_code = catalog_get_dir_oid_from_cache (thread_p, class_id_p, &dir_oid);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      catalog_access_info.class_oid = class_id_p;
      catalog_access_info.dir_oid = &dir_oid;
      error_code = catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, X_LOCK);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
      catalog_access_info_p = &catalog_access_info;
    }

  page_p = catalog_find_optimal_page (thread_p, CATALOG_CLS_INFO_SIZE, &page_id);
  if (page_p == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return error_code;
    }

  repr_item.page_id = page_id;

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error_code);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return error_code;
    }

  /* copy the given representation into a slotted page record */
  record.length = CATALOG_CLS_INFO_SIZE;
  catalog_put_class_info_to_record (record.data, class_info_p);
  memset ((char *) record.data + (CATALOG_CLS_INFO_SIZE - CATALOG_CLS_INFO_RESERVED), 0, CATALOG_CLS_INFO_RESERVED);

  success = spage_insert (thread_p, page_p, &record, &repr_item.slot_id);
  if (success != SP_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      recdes_free_data_area (&record);

      ASSERT_ERROR_AND_SET (error_code);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return error_code;
    }

  log_append_undoredo_recdes2 (thread_p, RVCT_INSERT, &catalog_Id.vfid, page_p, repr_item.slot_id, NULL, &record);

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  pgbuf_set_dirty (thread_p, page_p, FREE);

  catalog_update_max_space (&repr_item.page_id, new_space);

  if (catalog_put_representation_item (thread_p, class_id_p, &repr_item, &(class_info_p->ci_rep_dir)) != NO_ERROR)
    {
      recdes_free_data_area (&record);

      ASSERT_ERROR_AND_SET (error_code);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return error_code;
    }

  if (do_end_access)
    {
      catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, NO_ERROR);
    }

  recdes_free_data_area (&record);

  return NO_ERROR;
}

/*
 * catalog_update_class_info () - Update class information to the catalog
 *   return: CLS_INFO* or NULL
 *   class_id(in): Class identifier
 *   cls_info(in): Pointer to class specific information structure
 *   catalog_access_info_p(in): access info on catalog; if this is NULL
 *				locking of directory OID is performed here,
 *				otherwise the caller is reponsible for
 *				protecting concurrent access.
 *   skip_logging(in): true for skip logging. Otherwise, false
 *
 * Note: The given class specific information structure is used to
 * update existing class specific information. On success, it
 * returns the cls_info parameter itself, on failure it returns NULL.
 *
 * Note: This routine assumes that cls_info structure is of fixed size, ie.
 * the new record size is equal to the old record size, therefore it is
 * possible to update the old record in_place without moving.
 *
 * Note: The skip_logging parameter of the function may have FALSE value
 * in most cases. However it may have TRUE value in exceptional cases such as
 * the locator_increase_catalog_count() or locator_decrease_catalog_count().
 * Be sure that no log will be made when the parameter is TRUE.
 */
CLS_INFO *
catalog_update_class_info (THREAD_ENTRY * thread_p, OID * class_id_p, CLS_INFO * class_info_p,
			   CATALOG_ACCESS_INFO * catalog_access_info_p, bool skip_logging)
{
  PAGE_PTR page_p;
  char data[CATALOG_CLS_INFO_SIZE + MAX_ALIGNMENT], *aligned_data;
  RECDES record = { CATALOG_CLS_INFO_SIZE, CATALOG_CLS_INFO_SIZE, REC_HOME, NULL };
  CATALOG_REPR_ITEM repr_item = CATALOG_REPR_ITEM_INITIALIZER;
  LOG_DATA_ADDR addr;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;
  OID dir_oid;
  bool do_end_access = false;

  assert (class_info_p != NULL);
  assert (!OID_ISNULL (&(class_info_p->ci_rep_dir)));

  if (catalog_access_info_p == NULL)
    {
      do_end_access = true;
      if (catalog_get_dir_oid_from_cache (thread_p, class_id_p, &dir_oid) != NO_ERROR)
	{
	  return NULL;
	}

      catalog_access_info.class_oid = class_id_p;
      catalog_access_info.dir_oid = &dir_oid;
      if (catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, X_LOCK) != NO_ERROR)
	{
	  return NULL;
	}
      catalog_access_info_p = &catalog_access_info;
    }

  aligned_data = PTR_ALIGN (data, MAX_ALIGNMENT);

  assert (repr_item.repr_id == NULL_REPRID);
  repr_item.repr_id = NULL_REPRID;
  if (catalog_get_representation_item (thread_p, class_id_p, &repr_item) != NO_ERROR)
    {
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return NULL;
    }

  page_p = pgbuf_fix (thread_p, &repr_item.page_id, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  recdes_set_data_area (&record, aligned_data, CATALOG_CLS_INFO_SIZE);
  if (spage_get_record (page_p, repr_item.slot_id, &record, COPY) != S_SUCCESS)
    {
#if defined(CT_DEBUG)
      if (er_errid () == ER_SP_UNKNOWN_SLOTID)
	er_log_debug (ARG_FILE_LINE, "catalog_update_class_info: ",
		      "no class information record found in catalog.\n"
		      "possibly catalog index points to a non_existent disk repr.\n");
#endif /* CT_DEBUG */
      pgbuf_unfix_and_init (thread_p, page_p);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return NULL;
    }

  if (skip_logging != true)
    {
      log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, repr_item.slot_id, &record);
    }

  catalog_put_class_info_to_record (record.data, class_info_p);
  memset ((char *) record.data + (CATALOG_CLS_INFO_SIZE - CATALOG_CLS_INFO_RESERVED), 0, CATALOG_CLS_INFO_RESERVED);

  if (spage_update (thread_p, page_p, repr_item.slot_id, &record) != SP_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return NULL;
    }

  if (skip_logging != true)
    {
      log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, repr_item.slot_id, &record);
    }

  if (skip_logging == true)
    {
      addr.vfid = &catalog_Id.vfid;
      addr.pgptr = page_p;
      addr.offset = repr_item.slot_id;

      log_skip_logging (thread_p, &addr);
    }
  pgbuf_set_dirty (thread_p, page_p, FREE);
  if (do_end_access)
    {
      catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, NO_ERROR);
    }

  return class_info_p;
}

/*
 * catalog_drop () - Drop representation/class information from catalog
 *   return: int
 *   class_id(in): Class identifier
 *   repr_id(in): Representation identifier
 *
 *
 */
static int
catalog_drop (THREAD_ENTRY * thread_p, OID * class_id_p, REPR_ID repr_id)
{
  CATALOG_REPR_ITEM repr_item;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;
  OID dir_oid;
  int error_code = NO_ERROR;

  repr_item.repr_id = repr_id;
  VPID_SET_NULL (&repr_item.page_id);
  repr_item.slot_id = NULL_SLOTID;

  error_code = catalog_get_dir_oid_from_cache (thread_p, class_id_p, &dir_oid);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  catalog_access_info.class_oid = class_id_p;
  catalog_access_info.dir_oid = &dir_oid;
  error_code = catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, X_LOCK);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if ((catalog_drop_representation_item (thread_p, class_id_p, &repr_item) != NO_ERROR)
      || repr_item.page_id.pageid == NULL_PAGEID || repr_item.page_id.volid == NULL_VOLID
      || repr_item.slot_id == NULL_SLOTID)
    {
      ASSERT_ERROR_AND_SET (error_code);
      catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, ER_FAILED);
      return error_code;
    }

  if (catalog_drop_disk_representation_from_page (thread_p, &repr_item.page_id, repr_item.slot_id) != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error_code);
      catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, ER_FAILED);
      return error_code;
    }

  catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, NO_ERROR);

  return NO_ERROR;
}

/*
 * catalog_drop_all () - Drop all representations for the class from the catalog
 *   return: int
 *   class_id(in): Class identifier
 *
 * Note: All the disk representations of the given class identifier are
 * dropped from the catalog.
 */
static int
catalog_drop_all (THREAD_ENTRY * thread_p, OID * class_id_p)
{
  PAGE_PTR page_p;
  RECDES record;
  int i, repr_count;
  OID rep_dir;
  char *repr_p;
  REPR_ID repr_id;
  int error;

  OID_SET_NULL (&rep_dir);	/* init */

  if (OID_ISTEMP (class_id_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_CLASSID, 3, class_id_p->volid, class_id_p->pageid,
	      class_id_p->slotid);
      return ER_CT_INVALID_CLASSID;
    }

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  page_p = catalog_get_representation_record_after_search (thread_p, class_id_p, &record, PGBUF_LATCH_READ, COPY,
							   &rep_dir, &repr_count, true /* lookup_hash */ );
  if (page_p == NULL)
    {
      recdes_free_data_area (&record);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  repr_p = record.data;
  pgbuf_unfix_and_init (thread_p, page_p);

  /* drop each representation one by one */
  for (i = 0; i < repr_count; i++, repr_p += CATALOG_REPR_ITEM_SIZE)
    {
      repr_id = CATALOG_GET_REPR_ITEM_REPRID (repr_p);
      if (repr_id == NULL_REPRID)
	{
	  continue;
	}

      error = catalog_drop (thread_p, class_id_p, repr_id);
      if (error < 0)
	{
	  recdes_free_data_area (&record);
	  return error;
	}
    }

  recdes_free_data_area (&record);
  return NO_ERROR;
}

/*
 * catalog_drop_all_representation_and_class () -
 *   return: NO_ERROR or ER_FAILED
 *   class_id(in): Class identifier
 *
 * Note: All the disk representations of the given class identifier and
 * the class specific information of the class, if any, are
 * deleted from the catalog, as well as the class representations
 * directory and the index entry for the given class.
 * If there is no representation and class specific information
 * for the given class identifier, an error condition is raised.
 *
 * The catalog index is consulted to locate the class directory.
 * The class specific information and each representation
 * pointed to by the class directory is deleted from the catalog.
 */
static int
catalog_drop_all_representation_and_class (THREAD_ENTRY * thread_p, OID * class_id_p)
{
  PAGE_PTR page_p;
  PGLENGTH new_space;
  RECDES record;
  int i, repr_count;
  OID rep_dir;
  VPID vpid;
  char *repr_p;
  REPR_ID repr_id;
  VPID page_id;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;
  OID dir_oid;
  PGSLOTID slot_id;
  int error_code = NO_ERROR;

  OID_SET_NULL (&rep_dir);	/* init */

  if (OID_ISTEMP (class_id_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_CLASSID, 3, class_id_p->volid, class_id_p->pageid,
	      class_id_p->slotid);
      return ER_FAILED;
    }

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  error_code = catalog_get_dir_oid_from_cache (thread_p, class_id_p, &dir_oid);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  catalog_access_info.class_oid = class_id_p;
  catalog_access_info.dir_oid = &dir_oid;
  error_code = catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, X_LOCK);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  page_p = catalog_get_representation_record_after_search (thread_p, class_id_p, &record, PGBUF_LATCH_WRITE, COPY,
							   &rep_dir, &repr_count, true /* lookup_hash */ );
  if (page_p == NULL)
    {
      recdes_free_data_area (&record);
      catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, ER_FAILED);
      return ER_FAILED;
    }

  repr_p = record.data;
  vpid.volid = rep_dir.volid;
  vpid.pageid = rep_dir.pageid;

  /* drop each representation item one by one */
  for (i = 0; i < repr_count; i++, repr_p += CATALOG_REPR_ITEM_SIZE)
    {
      repr_id = CATALOG_GET_REPR_ITEM_REPRID (repr_p);
      page_id.pageid = CATALOG_GET_REPR_ITEM_PAGEID_PAGEID (repr_p);
      page_id.volid = CATALOG_GET_REPR_ITEM_PAGEID_VOLID (repr_p);
      slot_id = CATALOG_GET_REPR_ITEM_SLOTID (repr_p);

      catalog_delete_key (class_id_p, repr_id);

      if (catalog_drop_representation_class_from_page (thread_p, &vpid, &page_p, &page_id, slot_id) != NO_ERROR)
	{
	  if (page_p != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, page_p);
	    }
	  recdes_free_data_area (&record);
	  catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, ER_FAILED);
	  return ER_FAILED;
	}
      assert (page_p != NULL);
    }

  if (catalog_drop_directory (thread_p, page_p, &record, &rep_dir, class_id_p) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      recdes_free_data_area (&record);
      catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, ER_FAILED);
      return ER_FAILED;
    }

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  pgbuf_set_dirty (thread_p, page_p, FREE);
  catalog_update_max_space (&vpid, new_space);
  recdes_free_data_area (&record);

  catalog_delete_key (class_id_p, CATALOG_DIR_REPR_KEY);
  catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, NO_ERROR);

  return NO_ERROR;
}

/*
 * catalog_drop_old_representations () -
 *   return: NO_ERROR or ER_FAILED
 *   class_id(in): Class identifier
 *
 * Note: All the disk representations of the given class identifier but
 * the most recent one are deleted from the catalog. if there is
 * no such class stored in the catalog, an error condition is raised.
 */
int
catalog_drop_old_representations (THREAD_ENTRY * thread_p, OID * class_id_p)
{
  PAGE_PTR page_p;
  PGLENGTH new_space;
  RECDES record;
  int i, repr_count;
  OID rep_dir;
  VPID vpid;
  char *repr_p, *last_repr_p;
  REPR_ID repr_id;
  VPID page_id;
  PGSLOTID slot_id;
  CATALOG_REPR_ITEM last_repr, class_repr;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;
  OID dir_oid;
  int is_any_dropped;
  bool error_code = NO_ERROR;

  OID_SET_NULL (&rep_dir);	/* init */

  if (OID_ISTEMP (class_id_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_CLASSID, 3, class_id_p->volid, class_id_p->pageid,
	      class_id_p->slotid);
      return ER_FAILED;
    }

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  error_code = catalog_get_dir_oid_from_cache (thread_p, class_id_p, &dir_oid);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  catalog_access_info.class_oid = class_id_p;
  catalog_access_info.dir_oid = &dir_oid;
  error_code = catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, X_LOCK);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  page_p = catalog_get_representation_record_after_search (thread_p, class_id_p, &record, PGBUF_LATCH_WRITE, COPY,
							   &rep_dir, &repr_count, true /* lookup_hash */ );
  if (page_p == NULL)
    {
      recdes_free_data_area (&record);
      catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, ER_FAILED);
      return ER_FAILED;
    }

  repr_p = record.data;
  vpid.volid = rep_dir.volid;
  vpid.pageid = rep_dir.pageid;

  VPID_SET_NULL (&last_repr.page_id);
  VPID_SET_NULL (&class_repr.page_id);
  class_repr.repr_id = NULL_REPRID;
  class_repr.slot_id = NULL_SLOTID;

  if (repr_count > 0)
    {
      last_repr_p = repr_p + ((repr_count - 1) * CATALOG_REPR_ITEM_SIZE);
      catalog_get_repr_item_from_record (&last_repr, last_repr_p);

      if (last_repr.repr_id == NULL_REPRID && repr_count > 1)
	{
	  last_repr_p = repr_p + ((repr_count - 2) * CATALOG_REPR_ITEM_SIZE);
	  catalog_get_repr_item_from_record (&last_repr, last_repr_p);
	}
    }

  is_any_dropped = false;
  for (i = 0; i < repr_count; i++, repr_p += CATALOG_REPR_ITEM_SIZE)
    {
      repr_id = CATALOG_GET_REPR_ITEM_REPRID (repr_p);
      page_id.pageid = CATALOG_GET_REPR_ITEM_PAGEID_PAGEID (repr_p);
      page_id.volid = CATALOG_GET_REPR_ITEM_PAGEID_VOLID (repr_p);
      slot_id = CATALOG_GET_REPR_ITEM_SLOTID (repr_p);

      if (repr_id != NULL_REPRID && repr_id != last_repr.repr_id)
	{
	  catalog_delete_key (class_id_p, repr_id);

	  if (catalog_drop_representation_class_from_page (thread_p, &vpid, &page_p, &page_id, slot_id) != NO_ERROR)
	    {
	      if (page_p != NULL)
		{
		  pgbuf_unfix_and_init (thread_p, page_p);
		}
	      recdes_free_data_area (&record);
	      catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, ER_FAILED);
	      return ER_FAILED;
	    }
	  assert (page_p != NULL);

	  is_any_dropped = true;
	}
      else if (repr_id == NULL_REPRID)
	{
	  class_repr.repr_id = repr_id;
	  class_repr.page_id = page_id;
	  class_repr.slot_id = slot_id;
	}
    }

  /* update the class directory itself, if needed */
  if (is_any_dropped)
    {
      log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, rep_dir.slotid, &record);

      if (class_repr.page_id.pageid != NULL_PAGEID)
	{
	  /* first class info */
	  catalog_put_repr_item_to_record (record.data, &class_repr);
	  if (last_repr.page_id.pageid != NULL_PAGEID)
	    {
	      catalog_put_repr_item_to_record (record.data + CATALOG_REPR_ITEM_SIZE, &last_repr);
	      /* save #repr */
	      OR_PUT_BYTE (record.data + CATALOG_REPR_ITEM_COUNT_OFF, 2);
	    }
	  else
	    {
	      /* save #repr */
	      OR_PUT_BYTE (record.data + CATALOG_REPR_ITEM_COUNT_OFF, 1);
	    }
	}
      else if (last_repr.page_id.pageid != NULL_PAGEID)
	{
	  /* last repr item */
	  catalog_put_repr_item_to_record (record.data, &last_repr);
	  /* save #repr */
	  OR_PUT_BYTE (record.data + CATALOG_REPR_ITEM_COUNT_OFF, 1);
	}
      record.length = CATALOG_REPR_ITEM_SIZE * 2;

      if (spage_update (thread_p, page_p, rep_dir.slotid, &record) != SP_SUCCESS)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  recdes_free_data_area (&record);
	  catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, ER_FAILED);
	  return ER_FAILED;
	}

      log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p, rep_dir.slotid, &record);
    }

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  pgbuf_set_dirty (thread_p, page_p, FREE);
  catalog_update_max_space (&vpid, new_space);

  recdes_free_data_area (&record);
  catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, NO_ERROR);
  return NO_ERROR;
}

/*
 * xcatalog_check_rep_dir () -
 *   return: NO_ERROR or ER_FAILED
 *   class_id(in): Class identifier
 *   rep_dir_p(out): Representation Directory
 *
 * Note: Get oid of class representation record 
 */
int
xcatalog_check_rep_dir (THREAD_ENTRY * thread_p, OID * class_id_p, OID * rep_dir_p)
{
  RECDES record;
  PAGE_PTR page_p;
  int repr_count;

  assert (rep_dir_p != NULL);

  OID_SET_NULL (rep_dir_p);	/* init */

  record.area_size = -1;

  if (OID_ISTEMP (class_id_p))
    {
      assert (false);		/* should avoid */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_CLASSID, 3, class_id_p->volid, class_id_p->pageid,
	      class_id_p->slotid);
      return ER_FAILED;
    }

  page_p = catalog_get_representation_record_after_search (thread_p, class_id_p, &record, PGBUF_LATCH_WRITE, PEEK,
							   rep_dir_p, &repr_count, true /* lookup_hash */ );

  assert (er_errid () != ER_HEAP_NODATA_NEWADDRESS);	/* TODO - */

  if (page_p == NULL)
    {
      return ER_FAILED;
    }

  pgbuf_unfix_and_init (thread_p, page_p);

  assert (repr_count == 1 || repr_count == 2);

  if (OID_ISNULL (rep_dir_p))
    {
      assert (false);
      return ER_FAILED;
    }

  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * catalog_fixup_missing_disk_representation () -
 *   return:
 *   class_oid(in):
 *   reprid(in):
 */
static int
catalog_fixup_missing_disk_representation (THREAD_ENTRY * thread_p, OID * class_oid_p, REPR_ID repr_id)
{
  RECDES record;
  DISK_REPR *disk_repr_p;
  HEAP_SCANCACHE scan_cache;
  OID rep_dir = { NULL_PAGEID, NULL_SLOTID, NULL_VOLID };

  heap_scancache_quick_start_root_hfid (thread_p, &scan_cache);
  if (heap_get (thread_p, class_oid_p, &record, &scan_cache, PEEK, NULL_CHN) == S_SUCCESS)
    {
      disk_repr_p = orc_diskrep_from_record (thread_p, &record);
      if (disk_repr_p == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      or_class_rep_dir (&record, &rep_dir);
      assert (!OID_ISNULL (&rep_dir));

      if (catalog_add_representation (thread_p, class_oid_p, repr_id, disk_repr_p, &rep_dir) < 0)
	{
	  orc_free_diskrep (disk_repr_p);

	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      orc_free_diskrep (disk_repr_p);
    }

  heap_scancache_end (thread_p, &scan_cache);
  return NO_ERROR;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * catalog_assign_attribute () -
 *   return: NO_ERROR or ER_FAILED
 *   disk_attrp(in): pointer to DISK_ATTR structure (disk representation)
 *   catalog_record_p(in): pointer to CATALOG_RECORD structure (catalog record)
 */
static int
catalog_assign_attribute (THREAD_ENTRY * thread_p, DISK_ATTR * disk_attr_p, CATALOG_RECORD * catalog_record_p)
{
  BTREE_STATS *btree_stats_p;
  int i, n_btstats;

  if (catalog_fetch_disk_attribute (thread_p, disk_attr_p, catalog_record_p) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (disk_attr_p->val_length > 0)
    {
      disk_attr_p->value = db_private_alloc (thread_p, disk_attr_p->val_length);
      if (disk_attr_p->value == NULL)
	{
	  return ER_FAILED;
	}
    }

  if (catalog_fetch_attribute_value (thread_p, disk_attr_p->value, disk_attr_p->val_length, catalog_record_p) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  n_btstats = disk_attr_p->n_btstats;
  if (n_btstats > 0)
    {
      disk_attr_p->bt_stats = (BTREE_STATS *) db_private_alloc (thread_p, (sizeof (BTREE_STATS) * n_btstats));
      if (disk_attr_p->bt_stats == NULL)
	{
	  return ER_FAILED;
	}

      /* init */
      for (i = 0; i < n_btstats; i++)
	{
	  btree_stats_p = &disk_attr_p->bt_stats[i];
	  btree_stats_p->pkeys = NULL;
	}

      /* fetch all B+tree index statistics of the attribute */
      for (i = 0; i < n_btstats; i++)
	{
	  btree_stats_p = &disk_attr_p->bt_stats[i];

	  if (catalog_fetch_btree_statistics (thread_p, btree_stats_p, catalog_record_p) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * catalog_get_representation () - Get disk representation from the catalog
 *   return: Pointer to the disk representation structure, or NULL.
 *   class_id(in): Class identifier
 *   repr_id(in): Disk Representation identifier
 *   catalog_access_info_p(in): access info on catalog; if this is NULL
 *				locking of directory OID is performed here,
 *				otherwise the caller is reponsible for
 *				protecting concurrent access. 
 *
 * Note: The disk representation structure for the given class and
 * representation identifier is extracted from the catalog and
 * returned. If there is no representation with the given class
 * identifier and representation identifier, NULL is returned.
 * The memory area for the representation structure should be
 * freed explicitly by the caller, possibly by calling the
 * catalog_free_representation() routine.
 *
 * First, the memory hash table is consulted to get the location
 * of the representation in the catalog in terms of a page and a
 * slot identifier. If the memory hash table doesn't have this
 * information, then the catalog index is consulted to get the
 * location of the class representations directory in the catalog
 * The class directory is searched to locate the entry that
 * points to the specified disk representation in the catalog.
 * The location of the representation is recorded in the memory
 * hash table for further use. If the memory hash table becomes
 * full, it is cleared. The disk representation structure is
 * formed from the catalog page record and the overflow page
 * records, if any, and is returned.
 */
DISK_REPR *
catalog_get_representation (THREAD_ENTRY * thread_p, OID * class_id_p, REPR_ID repr_id,
			    CATALOG_ACCESS_INFO * catalog_access_info_p)
{
  CATALOG_REPR_ITEM repr_item;
  CATALOG_RECORD catalog_record;
  DISK_REPR *disk_repr_p = NULL;
  DISK_ATTR *disk_attr_p = NULL;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;
  OID dir_oid;
  int i, n_attrs;
  int error = NO_ERROR;
  bool do_end_access = false;

  catalog_record.page_p = NULL;

  if (catalog_access_info_p == NULL)
    {
      if (catalog_get_dir_oid_from_cache (thread_p, class_id_p, &dir_oid) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      catalog_access_info.class_oid = class_id_p;
      catalog_access_info.dir_oid = &dir_oid;
      if (catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, S_LOCK) != NO_ERROR)
	{
	  goto exit_on_error;
	}
      do_end_access = true;
      catalog_access_info_p = &catalog_access_info;
    }

  if (repr_id == NULL_REPRID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_REPRID, 1, repr_id);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return NULL;
    }

  repr_item.page_id.pageid = NULL_PAGEID;
  repr_item.page_id.volid = NULL_VOLID;
  repr_item.slot_id = NULL_SLOTID;

  assert (repr_id != NULL_REPRID);
  repr_item.repr_id = repr_id;
  if (catalog_get_representation_item (thread_p, class_id_p, &repr_item) != NO_ERROR)
    {
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return NULL;
    }

  catalog_record.vpid.pageid = repr_item.page_id.pageid;
  catalog_record.vpid.volid = repr_item.page_id.volid;
  catalog_record.slotid = repr_item.slot_id;
  catalog_record.page_p = NULL;
  catalog_record.recdes.length = 0;
  catalog_record.offset = 0;

  disk_repr_p = (DISK_REPR *) db_private_alloc (thread_p, sizeof (DISK_REPR));
  if (!disk_repr_p)
    {
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return NULL;
    }
  memset (disk_repr_p, 0, sizeof (DISK_REPR));

  if (catalog_fetch_disk_representation (thread_p, disk_repr_p, &catalog_record) != NO_ERROR)
    {
      if (disk_repr_p)
	{
	  catalog_free_representation (disk_repr_p);
	  disk_repr_p = NULL;
	}

      if (catalog_record.page_p)
	{
	  pgbuf_unfix_and_init (thread_p, catalog_record.page_p);
	}

      if (er_errid () == ER_SP_UNKNOWN_SLOTID)
	{
	  assert (false);
	}
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return NULL;
    }

  n_attrs = disk_repr_p->n_fixed + disk_repr_p->n_variable;

  if (disk_repr_p->n_fixed > 0)
    {
      disk_repr_p->fixed = (DISK_ATTR *) db_private_alloc (thread_p, (sizeof (DISK_ATTR) * disk_repr_p->n_fixed));
      if (!disk_repr_p->fixed)
	{
	  goto exit_on_error;
	}

      /* init */
      for (i = 0; i < disk_repr_p->n_fixed; i++)
	{
	  disk_attr_p = &disk_repr_p->fixed[i];
	  disk_attr_p->value = NULL;
	  disk_attr_p->bt_stats = NULL;
	  disk_attr_p->n_btstats = 0;
	}
    }
  else
    {
      disk_repr_p->fixed = NULL;
    }

  if (disk_repr_p->n_variable > 0)
    {
      disk_repr_p->variable = (DISK_ATTR *) db_private_alloc (thread_p, (sizeof (DISK_ATTR) * disk_repr_p->n_variable));
      if (!disk_repr_p->variable)
	{
	  goto exit_on_error;
	}

      /* init */
      for (i = disk_repr_p->n_fixed; i < n_attrs; i++)
	{
	  disk_attr_p = &disk_repr_p->variable[i - disk_repr_p->n_fixed];
	  disk_attr_p->value = NULL;
	  disk_attr_p->bt_stats = NULL;
	  disk_attr_p->n_btstats = 0;
	}
    }
  else
    {
      disk_repr_p->variable = NULL;
    }

  for (i = 0; i < disk_repr_p->n_fixed; i++)
    {
      if (catalog_assign_attribute (thread_p, &disk_repr_p->fixed[i], &catalog_record) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  for (i = 0; i < disk_repr_p->n_variable; i++)
    {
      if (catalog_assign_attribute (thread_p, &disk_repr_p->variable[i], &catalog_record) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

exit_on_end:

  if (catalog_record.page_p)
    {
      pgbuf_unfix_and_init (thread_p, catalog_record.page_p);
    }

  if (do_end_access)
    {
      catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, error);
    }

  return disk_repr_p;

exit_on_error:

  error = ER_FAILED;
  if (disk_repr_p)
    {
      catalog_free_representation (disk_repr_p);
      disk_repr_p = NULL;
    }

  goto exit_on_end;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
catalog_fixup_missing_class_info (THREAD_ENTRY * thread_p, OID * class_oid_p)
{
  RECDES record;
  HEAP_SCANCACHE scan_cache;
  CLS_INFO class_info = CLS_INFO_INITIALIZER;

  heap_scancache_quick_start_root_hfid (thread_p, &scan_cache);

  if (heap_get (thread_p, class_oid_p, &record, &scan_cache, PEEK, NULL_CHN) == S_SUCCESS)
    {
      or_class_hfid (&record, &(class_info.ci_hfid));
      or_class_rep_dir (&record, &(class_info.ci_rep_dir));
      assert (!OID_ISNULL (&(class_info.ci_rep_dir)));

      if (catalog_add_class_info (thread_p, class_oid_p, &class_info) < 0)
	{
	  heap_scancache_end (thread_p, &scan_cache);

	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }

  heap_scancache_end (thread_p, &scan_cache);

  return NO_ERROR;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * catalog_get_class_info () - Get class information from the catalog
 *   return: Pointer to the class information structure, or NULL.
 *   class_id_p(in): Class identifier
 *   catalog_access_info_p(in): access info on catalog; if this is NULL
 *				locking of directory OID is performed here,
 *				otherwise the caller is reponsible for
 *				protecting concurrent access.
 *
 * Note: The class information structure for the given class is
 * extracted from the catalog and returned. If there is no class
 * in the catalog with the given class identifier, NULL is returned.
 *
 * First, the memory hash table is consulted to get the location
 * of the class information  in the catalog in terms of a page
 * and a slot identifier. If the memory hash table doesn't have
 * this information, then the catalog index is consulted to get
 * location of the class representations directory in the catalog
 * The class directory is searched to locate the entry that
 * points to the specified class information in the catalog. The
 * ocation of the class information is recorded in the memory
 * hash table for further use. If the memory hash table becomes
 * full, it is cleared. The class information structure is formed
 * from the catalog page record and is returned.
 */
CLS_INFO *
catalog_get_class_info (THREAD_ENTRY * thread_p, OID * class_id_p, CATALOG_ACCESS_INFO * catalog_access_info_p)
{
  CLS_INFO *class_info_p = NULL;
  PAGE_PTR page_p;
  char data[CATALOG_CLS_INFO_SIZE + MAX_ALIGNMENT], *aligned_data;
  RECDES record = { CATALOG_CLS_INFO_SIZE, CATALOG_CLS_INFO_SIZE, REC_HOME, NULL };
  CATALOG_REPR_ITEM repr_item = CATALOG_REPR_ITEM_INITIALIZER;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;
  OID dir_oid;
#if 0
  int retry = 0;
#endif
  bool do_end_access = false;

  aligned_data = PTR_ALIGN (data, MAX_ALIGNMENT);

#if 0
start:
#endif

  if (catalog_access_info_p == NULL)
    {
      if (catalog_get_dir_oid_from_cache (thread_p, class_id_p, &dir_oid) != NO_ERROR)
	{
	  return NULL;
	}

      catalog_access_info.class_oid = class_id_p;
      catalog_access_info.dir_oid = &dir_oid;
      if (catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, S_LOCK) != NO_ERROR)
	{
	  return NULL;
	}
      do_end_access = true;
      catalog_access_info_p = &catalog_access_info;
    }

  assert (repr_item.repr_id == NULL_REPRID);
  repr_item.repr_id = NULL_REPRID;
  if (catalog_get_representation_item (thread_p, class_id_p, &repr_item) != NO_ERROR)
    {
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return NULL;
    }

  page_p = pgbuf_fix (thread_p, &repr_item.page_id, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  recdes_set_data_area (&record, aligned_data, CATALOG_CLS_INFO_SIZE);
  if (spage_get_record (page_p, repr_item.slot_id, &record, COPY) != S_SUCCESS)
    {
#if defined(CT_DEBUG)
      if (er_errid () == ER_SP_UNKNOWN_SLOTID)
	{
	  /* If this case happens, means, catalog doesn't contain a page slot which is referred to by index. It is
	   * possible that the slot has been deleted from the catalog page, but necessary changes have not been
	   * reflected to the index or to the memory hash table. */
	  er_log_debug (ARG_FILE_LINE, "catalog_get_class_info: ",
			"no class information record found in catalog.\n"
			"possibly catalog index points to a non_existent disk repr.\n");
	}
#endif /* CT_DEBUG */

      if (er_errid () == ER_SP_UNKNOWN_SLOTID)
	{
	  assert (false);
#if 0
	  pgbuf_unfix_and_init (thread_p, page_p);

	  if ((catalog_fixup_missing_class_info (thread_p, class_id_p) == NO_ERROR) && retry++ == 0)
	    {
	      goto start;
	    }
	  else
	    {
	      assert (0);
	      return NULL;
	    }
#endif
	}

      pgbuf_unfix_and_init (thread_p, page_p);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return NULL;
    }

  class_info_p = (CLS_INFO *) db_private_alloc (thread_p, sizeof (CLS_INFO));
  if (class_info_p == NULL)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      if (do_end_access)
	{
	  catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, ER_FAILED);
	}
      return NULL;
    }

  catalog_get_class_info_from_record (class_info_p, record.data);

  pgbuf_unfix_and_init (thread_p, page_p);

  if (do_end_access)
    {
      catalog_end_access_with_dir_oid (thread_p, catalog_access_info_p, NO_ERROR);
    }

  assert (!OID_ISNULL (&(class_info_p->ci_rep_dir)));

  return class_info_p;
}

/*
 * catalog_get_representation_directory () - Get class representations directory
 *   return: int
 *   class_id(in): Class identifier
 *   reprid_set(out): Set to the set of representation identifiers for the class
 *   repr_cnt(out): Set to the representation count
 *
 * Note: The set of representation identifiers for the specified class
 * is extracted from the catalog and returned in the memory area
 * allocated in this routine and pointed to by reprid_set. The
 * variable repr_cnt is set to the number of identifiers in this
 * set. The representation identifiers are in a chronological
 * order of sequence, the first one representing the oldest and
 * last one representing the most recent representation for the given class.
 */
int
catalog_get_representation_directory (THREAD_ENTRY * thread_p, OID * class_id_p, REPR_ID ** repr_id_set_p,
				      int *repr_count_p)
{
  OID rep_dir;
  PAGE_PTR page_p;
  RECDES record;
  int i, item_count;
  char *repr_p;
  REPR_ID *repr_set_p;
  size_t buf_size;

  OID_SET_NULL (&rep_dir);	/* init */

  *repr_count_p = 0;

  page_p = catalog_get_representation_record_after_search (thread_p, class_id_p, &record, PGBUF_LATCH_READ, PEEK,
							   &rep_dir, &item_count, true /* lookup_hash */ );
  if (page_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  *repr_count_p = item_count;
  buf_size = *repr_count_p * sizeof (REPR_ID);
  *repr_id_set_p = (REPR_ID *) malloc (buf_size);
  if (*repr_id_set_p == NULL)
    {
      pgbuf_unfix_and_init (thread_p, page_p);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  repr_set_p = *repr_id_set_p;

  for (i = 0, repr_p = record.data; i < item_count; i++, repr_p += CATALOG_REPR_ITEM_SIZE)
    {
      if (CATALOG_GET_REPR_ITEM_REPRID (repr_p) == NULL_REPRID)
	{
	  (*repr_count_p)--;
	}
      else
	{
	  *repr_set_p = CATALOG_GET_REPR_ITEM_REPRID (repr_p);
	  repr_set_p++;
	}
    }

  pgbuf_unfix_and_init (thread_p, page_p);
  return NO_ERROR;
}

/*
 * catalog_get_last_representation_id () -
 *   return: int
 *   cls_oid(in): Class identifier
 *   repr_id(out): Set to the last representation id or NULL_REPRID
 *
 * Note: The representation identifiers directory for the given class
 * is fetched from the catalog and the repr_id parameter is set
 * to the most recent representation identifier, or NULL_REPRID.
 */
int
catalog_get_last_representation_id (THREAD_ENTRY * thread_p, OID * class_oid_p, REPR_ID * repr_id_p)
{
  OID rep_dir;
  PAGE_PTR page_p;
  RECDES record;
  int i, item_count;
  char *repr_p;

  OID_SET_NULL (&rep_dir);	/* init */

  *repr_id_p = NULL_REPRID;

  page_p = catalog_get_representation_record_after_search (thread_p, class_oid_p, &record, PGBUF_LATCH_READ, PEEK,
							   &rep_dir, &item_count, true /* lookup_hash */ );
  if (page_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  for (i = 0, repr_p = record.data; i < item_count; i++, repr_p += CATALOG_REPR_ITEM_SIZE)
    {
      if (CATALOG_GET_REPR_ITEM_REPRID (repr_p) != NULL_REPRID)
	{
	  *repr_id_p = CATALOG_GET_REPR_ITEM_REPRID (repr_p);
	}
    }

  pgbuf_unfix_and_init (thread_p, page_p);
  return NO_ERROR;
}

/*
 * catalog_insert () - Insert class representation information to catalog
 *   return: int
 *   record(in): Record descriptor containing class disk representation info.
 *   classoid(in): Class object identifier
 *   rep_dir_p(out): Representation Directory
 *
 * Note: The disk representation information for the initial
 * representation of the class and the class specific information
 * is extracted from the record descriptor and stored in the
 * catalog. This routine must be the first routine called for the
 * storage of class representation informations in the catalog.
 */
int
catalog_insert (THREAD_ENTRY * thread_p, RECDES * record_p, OID * class_oid_p, OID * rep_dir_p)
{
  REPR_ID new_repr_id;
  DISK_REPR *disk_repr_p = NULL;
  CLS_INFO *class_info_p = NULL;

  assert (class_oid_p != NULL);
  assert (record_p != NULL);
  assert (rep_dir_p != NULL);
  assert (OID_ISNULL (rep_dir_p));

  new_repr_id = (REPR_ID) or_rep_id (record_p);
  if (new_repr_id == NULL_REPRID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_REPRID, 1, new_repr_id);
      return ER_CT_INVALID_REPRID;
    }

  disk_repr_p = orc_diskrep_from_record (thread_p, record_p);
  if (disk_repr_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  assert (OID_ISNULL (rep_dir_p));
  if (catalog_add_representation (thread_p, class_oid_p, new_repr_id, disk_repr_p, rep_dir_p, NULL) < 0
      || OID_ISNULL (rep_dir_p))
    {
      orc_free_diskrep (disk_repr_p);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  assert (!OID_ISNULL (rep_dir_p));

  orc_free_diskrep (disk_repr_p);

  class_info_p = orc_class_info_from_record (record_p);
  if (class_info_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* save representation directory oid into class_info record */
  assert (OID_ISNULL (&(class_info_p->ci_rep_dir)));
  COPY_OID (&(class_info_p->ci_rep_dir), rep_dir_p);

  if (catalog_add_class_info (thread_p, class_oid_p, class_info_p, NULL) < 0)
    {
      orc_free_class_info (class_info_p);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }
  orc_free_class_info (class_info_p);

  return NO_ERROR;
}

/*
 * catalog_update () - Update class representation information to catalog
 *   return: int
 *   record(in): Record descriptor containing class disk representation info.
 *   classoid(in): Class object identifier
 *
 * Note: The disk representation information for the specified class and
 * also possibly the class specific information is updated, if
 * the record descriptor contains a new disk representation, ie.
 * has a new representation identifier different from the most
 * recent representation identifier of the class. The class
 * information is extracted and only the HFID field is set, if
 * needed, in order to preserve old class statistics.
 */
int
catalog_update (THREAD_ENTRY * thread_p, RECDES * record_p, OID * class_oid_p)
{
  REPR_ID current_repr_id, new_repr_id;
  DISK_REPR *disk_repr_p = NULL;
  DISK_REPR *old_repr_p = NULL;
  CLS_INFO *class_info_p = NULL;
  OID rep_dir;

  new_repr_id = (REPR_ID) or_rep_id (record_p);
  if (new_repr_id == NULL_REPRID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_REPRID, 1, new_repr_id);
      return ER_CT_INVALID_REPRID;
    }

  or_class_rep_dir (record_p, &rep_dir);
  assert (!OID_ISNULL (&rep_dir));

  disk_repr_p = orc_diskrep_from_record (thread_p, record_p);
  if (disk_repr_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (catalog_get_last_representation_id (thread_p, class_oid_p, &current_repr_id) < 0)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (current_repr_id != NULL_REPRID)
    {
      old_repr_p = catalog_get_representation (thread_p, class_oid_p, current_repr_id, NULL);
      if (old_repr_p == NULL)
	{
	  if (er_errid () != ER_SP_UNKNOWN_SLOTID)
	    {
	      orc_free_diskrep (disk_repr_p);

	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	}

      /* Migrate statistics from the old representation to the new one */
      if (old_repr_p)
	{
	  catalog_copy_disk_attributes (disk_repr_p->fixed, disk_repr_p->n_fixed, old_repr_p->fixed,
					old_repr_p->n_fixed);
	  catalog_copy_disk_attributes (disk_repr_p->variable, disk_repr_p->n_variable, old_repr_p->variable,
					old_repr_p->n_variable);

	  catalog_free_representation (old_repr_p);
	  catalog_drop (thread_p, class_oid_p, current_repr_id);
	}
    }

  assert (!OID_ISNULL (&rep_dir));
  if (catalog_add_representation (thread_p, class_oid_p, new_repr_id, disk_repr_p, &rep_dir, NULL) < 0)
    {
      orc_free_diskrep (disk_repr_p);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  orc_free_diskrep (disk_repr_p);

  class_info_p = catalog_get_class_info (thread_p, class_oid_p, NULL);
  if (class_info_p != NULL)
    {
      assert (OID_EQ (&rep_dir, &(class_info_p->ci_rep_dir)));

      if (HFID_IS_NULL (&class_info_p->ci_hfid))
	{
	  or_class_hfid (record_p, &(class_info_p->ci_hfid));
	  if (!HFID_IS_NULL (&class_info_p->ci_hfid))
	    {
	      if (catalog_update_class_info (thread_p, class_oid_p, class_info_p, NULL, false) == NULL)
		{
		  catalog_free_class_info (class_info_p);

		  assert (er_errid () != NO_ERROR);
		  return er_errid ();
		}
	    }
	}
      catalog_free_class_info (class_info_p);
    }

  return NO_ERROR;
}

/*
 * catalog_delete () - Delete class representation information from catalog
 *   return: int
 *   classoid(in): Class object identifier
 *
 * Note: All the disk representation information for the class and the
 * class specific information are dropped from the catalog.
 */
int
catalog_delete (THREAD_ENTRY * thread_p, OID * class_oid_p)
{
  if (catalog_drop_all_representation_and_class (thread_p, class_oid_p) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  return NO_ERROR;
}

/*
 * Checkdb consistency check routines
 */

/*
 * catalog_check_class_consistency () -
 *   return: DISK_VALID, DISK_INVALID or DISK_ERROR
 *   class_oid_p(in): Class object identifier
 *
 * Note: This function checks the consistency of the class information
 * in the class and returns the consistency result. It checks
 * if the class points a valid page entry that
 * contains the representations directory for the class. It then
 * checks for each entry in the representations directory if
 * corresponding page slot entry exits.
 */
static DISK_ISVALID
catalog_check_class_consistency (THREAD_ENTRY * thread_p, OID * class_oid_p)
{
  OID rep_dir;
  RECDES record;
  CATALOG_REPR_ITEM repr_item;
  VPID vpid;
  PAGE_PTR page_p;
  PAGE_PTR repr_page_p;
  int repr_count;
  char *repr_p;
  CLS_INFO class_info = CLS_INFO_INITIALIZER;
  int i;
  DISK_ISVALID valid;

  OID_SET_NULL (&rep_dir);	/* init */

  record.area_size = -1;

  /* get old directory for the class */
  if (catalog_get_rep_dir (thread_p, class_oid_p, &rep_dir, true /* lookup_hash */ ) != NO_ERROR
      || OID_ISNULL (&rep_dir))
    {
      assert (er_errid () != NO_ERROR);
      return DISK_ERROR;
    }

  /* Check if the catalog index points to an existing representations directory. */

  vpid.volid = rep_dir.volid;
  vpid.pageid = rep_dir.pageid;
  valid = file_check_vpid (thread_p, &catalog_Id.vfid, &vpid);

  if (valid != DISK_VALID)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, rep_dir.volid, rep_dir.pageid,
		  rep_dir.slotid);
	}

      return valid;
    }

  page_p = catalog_get_representation_record (thread_p, &rep_dir, &record, PGBUF_LATCH_READ, PEEK, &repr_count);

  if (page_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_MISSING_REPR_DIR, 3, class_oid_p->volid, class_oid_p->pageid,
	      class_oid_p->slotid);
      return DISK_ERROR;
    }

  for (i = 0, repr_p = record.data; i < repr_count; i++, repr_p += CATALOG_REPR_ITEM_SIZE)
    {
      repr_item.page_id.pageid = CATALOG_GET_REPR_ITEM_PAGEID_PAGEID (repr_p);
      repr_item.page_id.volid = CATALOG_GET_REPR_ITEM_PAGEID_VOLID (repr_p);
      repr_item.slot_id = CATALOG_GET_REPR_ITEM_SLOTID (repr_p);
      repr_item.repr_id = CATALOG_GET_REPR_ITEM_REPRID (repr_p);

      valid = file_check_vpid (thread_p, &catalog_Id.vfid, &repr_item.page_id);
      if (valid != DISK_VALID)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  return valid;
	}

      repr_page_p = pgbuf_fix (thread_p, &repr_item.page_id, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (repr_page_p == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  return DISK_ERROR;
	}

      (void) pgbuf_check_page_ptype (thread_p, repr_page_p, PAGE_CATALOG);

      if (spage_get_record (repr_page_p, repr_item.slot_id, &record, PEEK) != S_SUCCESS)
	{
	  pgbuf_unfix_and_init (thread_p, repr_page_p);
	  pgbuf_unfix_and_init (thread_p, page_p);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_MISSING_REPR_INFO, 4, class_oid_p->volid, class_oid_p->pageid,
		  class_oid_p->slotid, repr_item.repr_id);
	  return DISK_ERROR;
	}

      /* is class info record */
      if (repr_item.repr_id == NULL_REPRID)
	{
	  catalog_get_class_info_from_record (&class_info, record.data);

	  if (!OID_EQ (&rep_dir, &(class_info.ci_rep_dir)))
	    {
	      assert (false);
	      pgbuf_unfix_and_init (thread_p, repr_page_p);
	      pgbuf_unfix_and_init (thread_p, page_p);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_MISSING_REPR_DIR, 3, class_oid_p->volid,
		      class_oid_p->pageid, class_oid_p->slotid);
	      return DISK_ERROR;
	    }
	}

      pgbuf_unfix_and_init (thread_p, repr_page_p);
    }

  pgbuf_unfix_and_init (thread_p, page_p);
  return DISK_VALID;
}

/*
 * catalog_check_consistency () - Check if catalog is in a consistent (valid)
 *                                state.
 *   return: DISK_VALID, DISK_INVALID or DISK_ERROR
 */
DISK_ISVALID
catalog_check_consistency (THREAD_ENTRY * thread_p)
{
  DISK_ISVALID ct_valid = DISK_VALID;
  RECDES peek = RECDES_INITIALIZER;	/* Record descriptor for peeking object */
  HFID root_hfid;
  OID class_oid;
#if !defined(NDEBUG)
  char *classname = NULL;
#endif
  HEAP_SCANCACHE scan_cache;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      return DISK_ERROR;
    }

  /* Find every single class */

  if (boot_find_root_heap (&root_hfid) != NO_ERROR || HFID_IS_NULL (&root_hfid))
    {
      goto exit_on_error;
    }

  if (heap_scancache_start (thread_p, &scan_cache, &root_hfid, oid_Root_class_oid, true, false, mvcc_snapshot) !=
      NO_ERROR)
    {
      goto exit_on_error;
    }

  class_oid.volid = root_hfid.vfid.volid;
  class_oid.pageid = NULL_PAGEID;
  class_oid.slotid = NULL_SLOTID;

  ct_valid = DISK_VALID;

  while (heap_next (thread_p, &root_hfid, oid_Root_class_oid, &class_oid, &peek, &scan_cache, PEEK) == S_SUCCESS)
    {
#if !defined(NDEBUG)
      classname = or_class_name (&peek);
      assert (classname != NULL);
      assert (strlen (classname) < 255);
#endif

      ct_valid = catalog_check_class_consistency (thread_p, &class_oid);
      if (ct_valid != DISK_VALID)
	{
	  break;
	}
    }				/* while (...) */

  /* End the scan cursor */
  if (heap_scancache_end (thread_p, &scan_cache) != NO_ERROR)
    {
      ct_valid = DISK_ERROR;
    }

  return ct_valid;

exit_on_error:

  return DISK_ERROR;
}

/*
 * catalog_dump_disk_attribute () -
 *   return:
 *   atr(in):
 */
static void
catalog_dump_disk_attribute (DISK_ATTR * attr_p)
{
  char *value_p;
  int i, k;
  const char *prefix = "";

  fprintf (stdout, "\n");
  fprintf (stdout, "Attribute Information: \n\n ");
  fprintf (stdout, "Id: %d \n", attr_p->id);
  fprintf (stdout, " Type: ");

  switch (attr_p->type)
    {
    case DB_TYPE_INTEGER:
      fprintf (stdout, "DB_TYPE_INTEGER \n");
      break;
    case DB_TYPE_BIGINT:
      fprintf (stdout, "DB_TYPE_BIGINT \n");
      break;
    case DB_TYPE_FLOAT:
      fprintf (stdout, "DB_TYPE_FLOAT \n");
      break;
    case DB_TYPE_DOUBLE:
      fprintf (stdout, "DB_TYPE_DOUBLE \n");
      break;
    case DB_TYPE_STRING:
      fprintf (stdout, "DB_TYPE_STRING \n");
      break;
    case DB_TYPE_OBJECT:
      fprintf (stdout, "DB_TYPE_OBJECT \n");
      break;
    case DB_TYPE_SET:
      fprintf (stdout, "DB_TYPE_SET \n");
      break;
    case DB_TYPE_MULTISET:
      fprintf (stdout, "DB_TYPE_MULTISET \n");
      break;
    case DB_TYPE_SEQUENCE:
      fprintf (stdout, "DB_TYPE_SEQUENCE \n");
      break;
    case DB_TYPE_TIME:
      fprintf (stdout, "DB_TYPE_TIME \n");
      break;
    case DB_TYPE_MONETARY:
      fprintf (stdout, "DB_TYPE_MONETARY \n");
      break;
    case DB_TYPE_DATE:
      fprintf (stdout, "DB_TYPE_DATE \n");
      break;
    case DB_TYPE_BLOB:
      fprintf (stdout, "DB_TYPE_BLOB \n");
      break;
    case DB_TYPE_CLOB:
      fprintf (stdout, "DB_TYPE_CLOB \n");
      break;
    case DB_TYPE_VARIABLE:
      fprintf (stdout, "DB_TYPE_VARIABLE \n");
      break;
    case DB_TYPE_SUB:
      fprintf (stdout, "DB_TYPE_SUB \n");
      break;
    case DB_TYPE_POINTER:
      fprintf (stdout, "DB_TYPE_POINTER \n");
      break;
    case DB_TYPE_NULL:
      fprintf (stdout, "DB_TYPE_NULL \n");
      break;
    default:
      break;
    }

  fprintf (stdout, " Location: %d \n", attr_p->location);
  fprintf (stdout, " Source Class_Id: { %d , %d , %d } \n", attr_p->classoid.volid, attr_p->classoid.pageid,
	   attr_p->classoid.slotid);
  fprintf (stdout, " Source Position: %d \n", attr_p->position);
  fprintf (stdout, " Def. Value Length: %d \n", attr_p->val_length);

  if (attr_p->val_length > 0)
    {
      value_p = (char *) attr_p->value;
      fprintf (stdout, " Value: ");

      for (k = 0; k < attr_p->val_length; k++, value_p++)
	{
	  fprintf (stdout, "%02X ", (unsigned char) (*value_p));
	}
      fprintf (stdout, " \n");
    }

  fprintf (stdout, " BTree statistics:\n");

  for (k = 0; k < attr_p->n_btstats; k++)
    {
      BTREE_STATS *bt_statsp = &attr_p->bt_stats[k];
      fprintf (stdout, "    BTID: { %d , %d }\n", bt_statsp->btid.vfid.volid, bt_statsp->btid.vfid.fileid);
      fprintf (stdout, "    Cardinality: %d (", bt_statsp->keys);

      prefix = "";
      assert (bt_statsp->pkeys_size <= BTREE_STATS_PKEYS_NUM);
      for (i = 0; i < bt_statsp->pkeys_size; i++)
	{
	  fprintf (stdout, "%s%d", prefix, bt_statsp->pkeys[i]);
	  prefix = ",";
	}

      fprintf (stdout, ") ,");
      fprintf (stdout, " Total Pages: %d , Leaf Pages: %d , Height: %d\n", bt_statsp->pages, bt_statsp->leafs,
	       bt_statsp->height);
    }

  fprintf (stdout, "\n");
}

/*
 * catalog_dump_representation () -
 *   return:
 *   dr(in):
 */
static void
catalog_dump_representation (DISK_REPR * disk_repr_p)
{
  DISK_ATTR *attr_p;
  int i;

  if (disk_repr_p != NULL)
    {
      fprintf (stdout, " DISK REPRESENTATION:  \n\n");
      fprintf (stdout, " Repr_Id : %d  N_Fixed : %d  Fixed_Length : %d  N_Variable : %d\n\n", disk_repr_p->id,
	       disk_repr_p->n_fixed, disk_repr_p->fixed_length, disk_repr_p->n_variable);

      fprintf (stdout, " Fixed Attribute Representations : \n\n");
      attr_p = disk_repr_p->fixed;
      for (i = 0; i < disk_repr_p->n_fixed; i++, attr_p++)
	{
	  catalog_dump_disk_attribute (attr_p);
	}

      fprintf (stdout, " Variable Attribute Representations : \n\n");
      attr_p = disk_repr_p->variable;
      for (i = 0; i < disk_repr_p->n_variable; i++, attr_p++)
	{
	  catalog_dump_disk_attribute (attr_p);
	}
    }
}

/*
 * catalog_file_map_page_dump () - FILE_MAP_PAGE_FUNC for catalog page dump
 *
 * return        : error code
 * thread_p (in) : thread entry
 * page (in)     : catalog page pointer
 * stop (in)     : not used
 * args (in)     : FILE *
 */
static int
catalog_file_map_page_dump (THREAD_ENTRY * thread_p, PAGE_PTR * page, bool * stop, void *args)
{
  CATALOG_PAGE_DUMP_CONTEXT *context = (CATALOG_PAGE_DUMP_CONTEXT *) args;
  RECDES record;

  if (spage_get_record (*page, CATALOG_HEADER_SLOT, &record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }

  fprintf (context->fp, "\n-----------------------------------------------\n");
  fprintf (context->fp, "\n Page %d \n", context->page_index);

  fprintf (context->fp, "\nPage_Id: {%d , %d}\n", PGBUF_PAGE_VPID_AS_ARGS (*page));
  fprintf (context->fp, "Directory Cnt: %d\n", CATALOG_GET_PGHEADER_DIR_COUNT (record.data));
  fprintf (context->fp, "Overflow Page Id: {%d , %d}\n\n", CATALOG_GET_PGHEADER_OVFL_PGID_PAGEID (record.data),
	   CATALOG_GET_PGHEADER_OVFL_PGID_VOLID (record.data));

  spage_dump (thread_p, context->fp, *page, 0);
  context->page_index++;
  return NO_ERROR;
}

/*
 * catalog_file_map_overflow_count () - FILE_MAP_PAGE_FUNC to count overflows
 *
 * return        : error code
 * thread_p (in) : thread entry
 * page (in)     : catalog page pointer
 * stop (in)     : not used
 * args (in)     : overflow count
 */
static int
catalog_file_map_overflow_count (THREAD_ENTRY * thread_p, PAGE_PTR * page, bool * stop, void *args)
{
  int *overflow_count = (int *) args;
  RECDES record;

  if (spage_get_record (*page, CATALOG_HEADER_SLOT, &record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }
  if (CATALOG_GET_PGHEADER_OVFL_PGID_PAGEID (record.data) != NULL_PAGEID)
    {
      (*overflow_count)++;
    }
  return NO_ERROR;
}

/*
 * catalog_dump () - The content of catalog is dumped.
 *   return: nothing
 *   dump_flg(in): Catalog dump flag. Should be set to:
 *                 0 : for catalog information content dump
 *                 1 : for catalog and catalog index slotted page dump
 */
void
catalog_dump (THREAD_ENTRY * thread_p, FILE * fp, int dump_flag)
{
  RECDES peek = RECDES_INITIALIZER;	/* Record descriptor for peeking object */
  HFID root_hfid;
  OID class_oid;
#if !defined(NDEBUG)
  char *classname = NULL;
#endif
  HEAP_SCANCACHE scan_cache;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  REPR_ID *repr_id_set, *repr_id_p;
  DISK_REPR *disk_repr_p = NULL;
  int repr_count;
  CLS_INFO *class_info_p = NULL;
  int n, overflow_count;

  CATALOG_PAGE_DUMP_CONTEXT page_dump_context;

  fprintf (fp, "\n <<<<< C A T A L O G   D U M P >>>>> \n\n");

  fprintf (fp, "\n Catalog Dump: \n\n");

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      return;
    }

  /* Find every single class */

  if (boot_find_root_heap (&root_hfid) != NO_ERROR || HFID_IS_NULL (&root_hfid))
    {
      return;
    }

  if (heap_scancache_start (thread_p, &scan_cache, &root_hfid, oid_Root_class_oid, true, false, mvcc_snapshot) !=
      NO_ERROR)
    {
      return;
    }

  class_oid.volid = root_hfid.vfid.volid;
  class_oid.pageid = NULL_PAGEID;
  class_oid.slotid = NULL_SLOTID;

  while (heap_next (thread_p, &root_hfid, oid_Root_class_oid, &class_oid, &peek, &scan_cache, PEEK) == S_SUCCESS)
    {
#if !defined(NDEBUG)
      classname = or_class_name (&peek);
      assert (classname != NULL);
      assert (strlen (classname) < 255);
#endif

      fprintf (fp, " -------------------------------------------------\n");
      fprintf (fp, " CLASS_ID: { %d , %d , %d } \n", class_oid.volid, class_oid.pageid, class_oid.slotid);

      repr_id_set = NULL;
      catalog_get_representation_directory (thread_p, &class_oid, &repr_id_set, &repr_count);
      fprintf (fp, " Repr_cnt: %d \n", repr_count);

      /* get the representations identifiers set for the class */
      repr_id_p = repr_id_set;
      for (repr_id_p += repr_count - 1; repr_count; repr_id_p--, repr_count--)
	{
	  if (*repr_id_p == NULL_REPRID)
	    {
	      continue;
	    }

	  fprintf (fp, " Repr_id: %d\n", *repr_id_p);
	}

      fprintf (fp, "\n");

      /* Get the class specific information for this class */
      class_info_p = catalog_get_class_info (thread_p, &class_oid, NULL);
      if (class_info_p != NULL)
	{
	  fprintf (fp, " Class Specific Information: \n\n");
	  fprintf (fp, " HFID: { vfid = { %d , %d }, hpgid = %d }\n", class_info_p->ci_hfid.vfid.fileid,
		   class_info_p->ci_hfid.vfid.volid, class_info_p->ci_hfid.hpgid);

	  fprintf (fp, " Total Pages in Heap: %d\n", class_info_p->ci_tot_pages);
	  fprintf (fp, " Total Objects: %d\n", class_info_p->ci_tot_objects);

	  fprintf (fp, " Representation directory OID: { %d , %d , %d } \n", class_info_p->ci_rep_dir.volid,
		   class_info_p->ci_rep_dir.pageid, class_info_p->ci_rep_dir.slotid);

	  catalog_free_class_info (class_info_p);
	}

      fprintf (fp, "\n");

      /* get the representations identifiers set for the class */
      repr_id_p = repr_id_set;
      for (repr_id_p += repr_count - 1; repr_count; repr_id_p--, repr_count--)
	{
	  if (*repr_id_p == NULL_REPRID)
	    {
	      continue;
	    }

	  disk_repr_p = catalog_get_representation (thread_p, &class_oid, *repr_id_p, NULL);
	  if (disk_repr_p == NULL)
	    {
	      continue;		/* is error */
	    }

	  catalog_dump_representation (disk_repr_p);
	  catalog_free_representation (disk_repr_p);
	}

      if (repr_id_set != NULL)
	{
	  free_and_init (repr_id_set);
	}

      heap_classrepr_dump_all (thread_p, fp, &class_oid);
    }				/* while (...) */

  /* End the scan cursor */
  if (heap_scancache_end (thread_p, &scan_cache) != NO_ERROR)
    {
      return;
    }

  if (dump_flag == 1)
    {
      /* slotted page dump */
      fprintf (fp, "\n Catalog Directory Dump: \n\n");

      if (file_get_num_user_pages (thread_p, &catalog_Id.vfid, &n) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return;
	}
      fprintf (fp, "Total Pages Count: %d\n\n", n);

      overflow_count = 0;
      if (file_map_pages (thread_p, &catalog_Id.vfid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH,
			  catalog_file_map_overflow_count, &overflow_count) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return;
	}

      fprintf (fp, "Regular Pages Count: %d\n\n", n - overflow_count);
      fprintf (fp, "Overflow Pages Count: %d\n\n", overflow_count);

      page_dump_context.fp = fp;
      page_dump_context.page_index = 0;
      if (file_map_pages (thread_p, &catalog_Id.vfid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH,
			  catalog_file_map_page_dump, &page_dump_context) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return;
	}
    }
}

static void
catalog_clear_hash_table (void)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (NULL, THREAD_TS_CATALOG);

  lf_hash_clear (t_entry, &catalog_Hash_table);
}


/*
 * catalog_rv_new_page_redo () - Redo the initializations of a new catalog page
 *   return: int
 *   recv(in): Recovery structure
 */
int
catalog_rv_new_page_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  char data[CATALOG_PAGE_HEADER_SIZE + MAX_ALIGNMENT], *aligned_data;
  RECDES record = { CATALOG_PAGE_HEADER_SIZE, CATALOG_PAGE_HEADER_SIZE, REC_HOME, NULL };
  PGSLOTID slot_id;
  int success;

  aligned_data = PTR_ALIGN (data, MAX_ALIGNMENT);

  catalog_clear_hash_table ();

  (void) pgbuf_set_page_ptype (thread_p, recv_p->pgptr, PAGE_CATALOG);

  spage_initialize (thread_p, recv_p->pgptr, ANCHORED_DONT_REUSE_SLOTS, MAX_ALIGNMENT, SAFEGUARD_RVSPACE);

  recdes_set_data_area (&record, aligned_data, CATALOG_PAGE_HEADER_SIZE);
  catalog_put_page_header (record.data, (CATALOG_PAGE_HEADER *) recv_p->data);
  success = spage_insert (thread_p, recv_p->pgptr, &record, &slot_id);

  if (success != SP_SUCCESS || slot_id != CATALOG_HEADER_SLOT)
    {
      if (success != SP_SUCCESS)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	}
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, recv_p->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * catalog_rv_insert_redo () - Redo the insertion of a record at a specific slot.
 *   return: int
 *   recv(in): Recovery structure
 */
int
catalog_rv_insert_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  PGSLOTID slot_id;
  RECDES record;
  int success;

  catalog_clear_hash_table ();

  slot_id = recv_p->offset;

  recdes_set_data_area (&record, (char *) (recv_p->data) + sizeof (record.type), recv_p->length - sizeof (record.type));
  record.length = record.area_size;
  record.type = *(INT16 *) (recv_p->data);

  success = spage_insert_for_recovery (thread_p, recv_p->pgptr, slot_id, &record);
  if (success != SP_SUCCESS)
    {
      if (success != SP_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	}
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, recv_p->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * catalog_rv_insert_undo () - Undo the insert of a record by deleting the record.
 *   return: int
 *   recv(in): Recovery structure
 */
int
catalog_rv_insert_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  PGSLOTID slot_id;

  catalog_clear_hash_table ();

  slot_id = recv_p->offset;
  (void) spage_delete_for_recovery (thread_p, recv_p->pgptr, slot_id);
  pgbuf_set_dirty (thread_p, recv_p->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * catalog_rv_delete_redo () - Redo the deletion of a record.
 *   return: int
 *   recv(in): Recovery structure
 */
int
catalog_rv_delete_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  PGSLOTID slot_id;

  catalog_clear_hash_table ();

  slot_id = recv_p->offset;
  (void) spage_delete (thread_p, recv_p->pgptr, slot_id);
  pgbuf_set_dirty (thread_p, recv_p->pgptr, DONT_FREE);

  return NO_ERROR;
}


/*
 * catalog_get_cardinality () - gets the cardinality of an index using
 *			      class OID, class DISK_REPR, index BTID
 *			      and partial key count
 *   return: NO_ERROR, or error code
 *   thread_p(in)  : thread context
 *   class_oid(in): class OID
 *   disk_repr_p(in): class DISK_REPR
 *   btid(in): index BTID
 *   key_pos(in)   : partial key (i-th column from index definition)
 *   cardinality(out): number of distinct values
 *
 */
int
catalog_get_cardinality (THREAD_ENTRY * thread_p, OID * class_oid, DISK_REPR * rep, BTID * btid, const int key_pos,
			 int *cardinality)
{
  int idx_cnt;
  int att_cnt;
  int error = NO_ERROR;
  BTREE_STATS *p_stat_info = NULL;
  BTREE_STATS *curr_stat_info = NULL;
  DISK_ATTR *disk_attr_p = NULL;
  BTID curr_bitd;
  bool is_btree_found;
  DISK_REPR *disk_repr_p = NULL;
  bool free_disk_rep = false;
  int key_size;
  OID *partitions = NULL;
  int count = 0;
  CLS_INFO *subcls_info = NULL;
  DISK_REPR *subcls_disk_rep = NULL;
  OR_CLASSREP *subcls_rep = NULL;
  OR_CLASSREP *cls_rep = NULL;
  DISK_ATTR *subcls_attr = NULL;
  const BTREE_STATS *subcls_stats = NULL;
  REPR_ID subcls_repr_id;
  int subcls_idx_cache;
  int idx_cache;
  int i;
  bool is_subcls_attr_found = false;
  bool free_cls_rep = false;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;
  OID dir_oid;

  assert (class_oid != NULL && btid != NULL && cardinality != NULL);
  *cardinality = -1;

  if (rep != NULL)
    {
      disk_repr_p = rep;
    }
  else
    {
      int repr_id;

      error = catalog_get_dir_oid_from_cache (thread_p, class_oid, &dir_oid);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      catalog_access_info.class_oid = class_oid;
      catalog_access_info.dir_oid = &dir_oid;
      error = catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, S_LOCK);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      /* get last representation id, if is not already known */
      error = catalog_get_last_representation_id (thread_p, class_oid, &repr_id);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      /* get disk representation : the disk representation contains a some pre-filled BTREE statistics, but no partial
       * keys info yet */
      disk_repr_p = catalog_get_representation (thread_p, class_oid, repr_id, &catalog_access_info);
      if (disk_repr_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 1, "Disk representation not found.");
	  error = ER_UNEXPECTED;
	  goto exit;
	}
      catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, NO_ERROR);
      free_disk_rep = true;
    }

  cls_rep = heap_classrepr_get (thread_p, class_oid, NULL, NULL_REPRID, &idx_cache);
  if (cls_rep == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 1, "Class representation not found.");
      error = ER_UNEXPECTED;
      goto exit_cleanup;
    }
  free_cls_rep = true;

  /* There should be only one OR_ATTRIBUTE element that contains the index; this is the element corresponding to the
   * attribute which is the first key in the index */
  p_stat_info = NULL;
  is_btree_found = false;
  /* first, search in the fixed attributes : */
  for (att_cnt = 0, disk_attr_p = disk_repr_p->fixed; att_cnt < disk_repr_p->n_fixed; att_cnt++, disk_attr_p++)
    {
      /* search for BTID in each BTREE_STATS element from current attribute */
      for (idx_cnt = 0, curr_stat_info = disk_attr_p->bt_stats; idx_cnt < disk_attr_p->n_btstats;
	   idx_cnt++, curr_stat_info++)
	{
	  curr_bitd = curr_stat_info->btid;
	  if (BTID_IS_EQUAL (&curr_bitd, btid))
	    {
	      p_stat_info = curr_stat_info;
	      is_btree_found = true;
	      break;
	    }
	}
      if (is_btree_found)
	{
	  break;
	}
    }

  if (!is_btree_found)
    {
      assert_release (p_stat_info == NULL);
      /* not found, repeat the search for variable attributes */
      for (att_cnt = 0, disk_attr_p = disk_repr_p->variable; att_cnt < disk_repr_p->n_variable;
	   att_cnt++, disk_attr_p++)
	{
	  /* search for BTID in each BTREE_STATS element from current attribute */
	  for (idx_cnt = 0, curr_stat_info = disk_attr_p->bt_stats; idx_cnt < disk_attr_p->n_btstats;
	       idx_cnt++, curr_stat_info++)
	    {
	      curr_bitd = curr_stat_info->btid;
	      if (BTID_IS_EQUAL (&curr_bitd, btid))
		{
		  p_stat_info = curr_stat_info;
		  is_btree_found = true;
		  break;
		}
	    }
	  if (is_btree_found)
	    {
	      break;
	    }
	}
    }

  if (!is_btree_found)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 1, "B-Tree not found.");
      error = ER_UNEXPECTED;
      goto exit_cleanup;
    }

  assert_release (p_stat_info != NULL);
  assert_release (BTID_IS_EQUAL (&(p_stat_info->btid), btid));
  assert_release (p_stat_info->pkeys_size > 0);
  assert_release (p_stat_info->pkeys_size <= BTREE_STATS_PKEYS_NUM);

  /* since btree_get_stats is too slow, use the old statistics. the user must previously execute 'update statistics on
   * class_name', in order to get updated statistics. */

  if (TP_DOMAIN_TYPE (p_stat_info->key_type) == DB_TYPE_MIDXKEY)
    {
      key_size = tp_domain_size (p_stat_info->key_type->setdomain);
    }
  else
    {
      key_size = 1;
    }

  if (key_pos >= key_size || key_pos < 0)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR, 1, "index_cardinality()");
      goto exit_cleanup;
    }

  error = partition_get_partition_oids (thread_p, class_oid, &partitions, &count);
  if (error != NO_ERROR)
    {
      goto exit_cleanup;
    }

  if (count == 0)
    {
      *cardinality = p_stat_info->keys;
    }
  else
    {
      *cardinality = 0;
      for (i = 0; i < count; i++)
	{
	  /* clean subclass loaded in previous iteration */
	  if (subcls_info != NULL)
	    {
	      catalog_free_class_info (subcls_info);
	      subcls_info = NULL;
	    }
	  if (subcls_disk_rep != NULL)
	    {
	      catalog_free_representation (subcls_disk_rep);
	      subcls_disk_rep = NULL;
	    }
	  if (subcls_rep != NULL)
	    {
	      heap_classrepr_free (subcls_rep, &subcls_idx_cache);
	      subcls_rep = NULL;
	    }

	  if (catalog_get_dir_oid_from_cache (thread_p, &partitions[i], &dir_oid) != NO_ERROR)
	    {
	      continue;
	    }

	  catalog_access_info.class_oid = &partitions[i];
	  catalog_access_info.dir_oid = &dir_oid;
	  if (catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, S_LOCK) != NO_ERROR)
	    {
	      goto exit_cleanup;
	    }

	  /* load new subclass */
	  subcls_info = catalog_get_class_info (thread_p, &partitions[i], &catalog_access_info);
	  if (subcls_info == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto exit_cleanup;
	    }

	  /* get disk repr for subclass */
	  error = catalog_get_last_representation_id (thread_p, &partitions[i], &subcls_repr_id);
	  if (error != NO_ERROR)
	    {
	      goto exit_cleanup;
	    }

	  subcls_disk_rep = catalog_get_representation (thread_p, &partitions[i], subcls_repr_id, &catalog_access_info);
	  if (subcls_disk_rep == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto exit_cleanup;
	    }
	  catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, NO_ERROR);

	  subcls_rep = heap_classrepr_get (thread_p, &partitions[i], NULL, NULL_REPRID, &subcls_idx_cache);
	  if (subcls_rep == NULL)
	    {
	      error = ER_FAILED;
	      goto exit_cleanup;
	    }

	  is_subcls_attr_found = false;
	  for (att_cnt = 0, subcls_attr = subcls_disk_rep->fixed; att_cnt < subcls_disk_rep->n_fixed;
	       att_cnt++, subcls_attr++)
	    {
	      if (disk_attr_p->id == subcls_attr->id)
		{
		  is_subcls_attr_found = true;
		  break;
		}
	    }
	  if (!is_subcls_attr_found)
	    {
	      for (att_cnt = 0, subcls_attr = subcls_disk_rep->variable; att_cnt < subcls_disk_rep->n_variable;
		   att_cnt++, subcls_attr++)
		{
		  if (disk_attr_p->id == subcls_attr->id)
		    {
		      is_subcls_attr_found = true;
		      break;
		    }
		}
	    }

	  if (!is_subcls_attr_found)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 1, "Attribute of subclass not found.");
	      error = ER_UNEXPECTED;
	      goto exit_cleanup;
	    }
	  subcls_stats = stats_find_inherited_index_stats (cls_rep, subcls_rep, subcls_attr, btid);
	  if (subcls_stats == NULL)
	    {
	      error = ER_FAILED;
	      goto exit_cleanup;
	    }

	  *cardinality = *cardinality + subcls_stats->keys;
	}
    }

exit_cleanup:
  if (free_disk_rep)
    {
      catalog_free_representation (disk_repr_p);
    }
  if (free_cls_rep)
    {
      heap_classrepr_free (cls_rep, &idx_cache);
    }
  if (subcls_info != NULL)
    {
      catalog_free_class_info (subcls_info);
      subcls_info = NULL;
    }
  if (subcls_disk_rep != NULL)
    {
      catalog_free_representation (subcls_disk_rep);
      subcls_disk_rep = NULL;
    }
  if (subcls_rep != NULL)
    {
      heap_classrepr_free (subcls_rep, &subcls_idx_cache);
      subcls_rep = NULL;
    }
  if (partitions != NULL)
    {
      db_private_free (thread_p, partitions);
    }
exit:
  catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, error);
  return error;

}

/*
 * catalog_get_cardinality_by_name () - gets the cardinality of an index using
 *				      its name and partial key count
 *   return: NO_ERROR, or error code
 *   thread_p(in)  : thread context
 *   class_name(in): name of class
 *   index_name(in): name of index
 *   key_pos(in)   : partial key (i-th column from index definition)
 *   cardinality(out): number of distinct values
 *
 */
int
catalog_get_cardinality_by_name (THREAD_ENTRY * thread_p, const char *class_name, const char *index_name,
				 const int key_pos, int *cardinality)
{
  int error = NO_ERROR;
  BTID found_btid;
  BTID curr_bitd;
  OID class_oid;
  char cls_lower[DB_MAX_IDENTIFIER_LENGTH] = { 0 };
  LC_FIND_CLASSNAME status;

  BTID_SET_NULL (&found_btid);
  BTID_SET_NULL (&curr_bitd);

  assert (class_name != NULL);
  assert (index_name != NULL);
  assert (cardinality != NULL);

  *cardinality = -1;

  /* get class OID from class name */
  intl_identifier_lower (class_name, cls_lower);

  status = xlocator_find_class_oid (thread_p, cls_lower, &class_oid, NULL_LOCK);
  if (status == LC_CLASSNAME_ERROR)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LC_UNKNOWN_CLASSNAME, 1, cls_lower);
      return ER_FAILED;
    }

  if (status == LC_CLASSNAME_DELETED || OID_ISNULL (&class_oid))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LC_UNKNOWN_CLASSNAME, 1, class_name);
      goto exit;
    }

  if (lock_object (thread_p, &class_oid, oid_Root_class_oid, SCH_S_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
    {
      error = ER_FAILED;
      goto exit;
    }

  error = heap_get_btid_from_index_name (thread_p, &class_oid, index_name, &found_btid);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  if (BTID_IS_NULL (&found_btid))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INDEX_NOT_FOUND, 0);
      goto exit;
    }

  return catalog_get_cardinality (thread_p, &class_oid, NULL, &found_btid, key_pos, cardinality);

exit:
  return error;
}

/*
 * catalog_rv_delete_undo () - Undo the deletion of a record.
 *   return: int
 *   recv(in): Recovery structure
 */
int
catalog_rv_delete_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  catalog_clear_hash_table ();
  return catalog_rv_insert_redo (thread_p, recv_p);
}

/*
 * catalog_rv_update () - Recover an update either for undo or redo
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover an update to a record in a slotted page
 */
int
catalog_rv_update (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  PGSLOTID slot_id;
  RECDES record;

  catalog_clear_hash_table ();

  slot_id = recv_p->offset;

  recdes_set_data_area (&record, (char *) (recv_p->data) + sizeof (record.type), recv_p->length - sizeof (record.type));
  record.length = record.area_size;
  record.type = *(INT16 *) (recv_p->data);

  if (spage_update (thread_p, recv_p->pgptr, slot_id, &record) != SP_SUCCESS)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, recv_p->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * catalog_rv_ovf_page_logical_insert_undo () - Undo new overflow page creation.
 *   return: int
 *   recv(in): Recovery structure
 */
int
catalog_rv_ovf_page_logical_insert_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv_p)
{
  VPID *vpid_p;

  catalog_clear_hash_table ();

  vpid_p = (VPID *) recv_p->data;
  return file_dealloc (thread_p, &catalog_Id.vfid, vpid_p, FILE_CATALOG);
}

/*
 * catalog_get_dir_oid_from_cache () - Get directory OID from cache
 *				       or class record
 *   return: error status
 *   class_id_p(in): Class identifier
 *   dir_oid_p(out): directory OID
 *
 */
int
catalog_get_dir_oid_from_cache (THREAD_ENTRY * thread_p, const OID * class_id_p, OID * dir_oid_p)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_CATALOG);
  CATALOG_ENTRY *catalog_value_p;
  CATALOG_KEY catalog_key;
  HEAP_SCANCACHE scan_cache;
  RECDES record = { -1, -1, REC_HOME, NULL };
  int error = NO_ERROR;

  assert (dir_oid_p != NULL);
  OID_SET_NULL (dir_oid_p);

  catalog_key.page_id = class_id_p->pageid;
  catalog_key.volid = class_id_p->volid;
  catalog_key.slot_id = class_id_p->slotid;
  catalog_key.repr_id = CATALOG_DIR_REPR_KEY;

  if (lf_hash_find (t_entry, &catalog_Hash_table, (void *) &catalog_key, (void **) &catalog_value_p) != NO_ERROR)
    {
      return ER_FAILED;
    }
  else if (catalog_value_p != NULL)
    {
      /* entry already exists */
      dir_oid_p->volid = catalog_value_p->key.r_page_id.volid;
      dir_oid_p->pageid = catalog_value_p->key.r_page_id.pageid;
      dir_oid_p->slotid = catalog_value_p->key.r_slot_id;

      /* end transaction */
      lf_tran_end_with_mb (t_entry);
      return NO_ERROR;
    }

  /* not found in cache, get it from class record */
  heap_scancache_quick_start_root_hfid (thread_p, &scan_cache);

  if (heap_get_class_record (thread_p, class_id_p, &record, &scan_cache, PEEK) == S_SUCCESS)
    {
      or_class_rep_dir (&record, dir_oid_p);
    }
  else
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}

      heap_scancache_end (thread_p, &scan_cache);

      return error;
    }

  heap_scancache_end (thread_p, &scan_cache);

  if (OID_ISNULL (dir_oid_p))
    {
      /* directory not created yet, don't cache NULL OID */
      return NO_ERROR;
    }

  catalog_key.r_page_id.pageid = dir_oid_p->pageid;
  catalog_key.r_page_id.volid = dir_oid_p->volid;
  catalog_key.r_slot_id = dir_oid_p->slotid;

  /* insert value */
  if (lf_hash_find_or_insert (t_entry, &catalog_Hash_table, (void *) &catalog_key, (void **) &catalog_value_p, NULL)
      != NO_ERROR)
    {
      return ER_FAILED;
    }
  else if (catalog_value_p != NULL)
    {
      lf_tran_end_with_mb (t_entry);
      return NO_ERROR;
    }
  else
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * catalog_start_access_with_dir_oid () - starts an access on catalog using
 *					  directory OID for locking purpose
 *   return: error code
 *   catalog_access_info(in/out): catalog access helper structure
 *   lock_mode(in): should be X_LOCK for update on catalog and S_LOCK for read
 */
int
catalog_start_access_with_dir_oid (THREAD_ENTRY * thread_p, CATALOG_ACCESS_INFO * catalog_access_info, LOCK lock_mode)
{
#if defined (SERVER_MODE)
  LOCK current_lock;
#endif /* SERVER_MODE */
  int error_code = NO_ERROR;
  int lk_grant_code;
  OID virtual_class_dir_oid;

  assert (catalog_access_info != NULL);

  assert (catalog_access_info->access_started == false);
  if (catalog_access_info->access_started == true)
    {
      (void) catalog_end_access_with_dir_oid (thread_p, catalog_access_info, NO_ERROR);
    }

  if (BO_IS_SERVER_RESTARTED () == false || OID_ISNULL (catalog_access_info->dir_oid))
    {
      /* server not started or class dir not created yet: do not use locking */
      return NO_ERROR;
    }

  catalog_access_info->need_unlock = false;
  catalog_access_info->is_update = (lock_mode == X_LOCK) ? true : false;

  if (lock_mode == X_LOCK)
    {
      log_sysop_start (thread_p);
#if !defined (NDEBUG)
      catalog_access_info->is_systemop_started = true;
#endif
    }

  OID_GET_VIRTUAL_CLASS_OF_DIR_OID (catalog_access_info->class_oid, &virtual_class_dir_oid);
#if defined (SERVER_MODE)
  current_lock =
    lock_get_object_lock (catalog_access_info->dir_oid, &virtual_class_dir_oid, LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  if (current_lock != NULL_LOCK)
    {
      assert (false);

      if (lock_mode == X_LOCK)
	{
	  log_sysop_abort (thread_p);
	}

      error_code = ER_FAILED;
      return error_code;
    }
#endif /* SERVER_MODE */

  /* before go further, we should get the lock to disable updating schema */
  lk_grant_code =
    lock_object (thread_p, catalog_access_info->dir_oid, &virtual_class_dir_oid, lock_mode, LK_UNCOND_LOCK);
  if (lk_grant_code != LK_GRANTED)
    {
      assert (lk_grant_code == LK_NOTGRANTED_DUE_ABORTED || lk_grant_code == LK_NOTGRANTED_DUE_TIMEOUT);
      if (catalog_access_info->class_name == NULL)
	{
	  catalog_access_info->class_name = heap_get_class_name (thread_p, catalog_access_info->class_oid);
	  if (catalog_access_info->class_name != NULL)
	    {
	      catalog_access_info->need_free_class_name = true;
	    }
	}

      if (lock_mode == X_LOCK)
	{
	  log_sysop_abort (thread_p);
	}

#if !defined (NDEBUG)
      catalog_access_info->is_systemop_started = false;
#endif

      error_code = ER_UPDATE_STAT_CANNOT_GET_LOCK;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1,
	      catalog_access_info->class_name ? catalog_access_info->class_name : "*UNKNOWN-CLASS*");

      goto error;
    }

  catalog_access_info->need_unlock = true;
  catalog_access_info->access_started = true;

  return error_code;

error:
  return error_code;
}

/*
 * catalog_end_access_with_dir_oid () - ends access on catalog using directory
 *					OID
 *   return: error code
 *   catalog_access_info(in/out): catalog access helper structure
 *   error(in): error code
 */
int
catalog_end_access_with_dir_oid (THREAD_ENTRY * thread_p, CATALOG_ACCESS_INFO * catalog_access_info, int error)
{
  OID virtual_class_dir_oid;
  LOCK current_lock;

  assert (catalog_access_info != NULL);

  if (catalog_access_info->access_started == false)
    {
      assert (catalog_access_info->need_unlock == false);
#if !defined (NDEBUG)
      assert (catalog_access_info->is_systemop_started == false);
#endif
      return NO_ERROR;
    }

  assert (BO_IS_SERVER_RESTARTED () == true);

  OID_GET_VIRTUAL_CLASS_OF_DIR_OID (catalog_access_info->class_oid, &virtual_class_dir_oid);
  if (catalog_access_info->need_unlock == true)
    {
      current_lock = catalog_access_info->is_update ? X_LOCK : S_LOCK;
      lock_unlock_object_donot_move_to_non2pl (thread_p, catalog_access_info->dir_oid, &virtual_class_dir_oid,
					       current_lock);
    }

  if (catalog_access_info->is_update == true)
    {
      if (error != NO_ERROR)
	{
	  log_sysop_abort (thread_p);
	}
      else
	{
#if defined (SERVER_MODE)
	  current_lock =
	    lock_get_object_lock (catalog_access_info->class_oid, oid_Root_class_oid,
				  LOG_FIND_THREAD_TRAN_INDEX (thread_p));

	  if (current_lock == SCH_M_LOCK)
	    {
	      /* when class was created or schema was changed commit the statistics changes along with schema change */
	      log_sysop_attach_to_outer (thread_p);
	    }
	  else
	    {
	      /* this case applies with UPDATE STATISTICS */
	      log_sysop_commit (thread_p);
	    }
#else
	  log_sysop_attach_to_outer (thread_p);
#endif /* SERVER_MODE */
	}
#if !defined (NDEBUG)
      catalog_access_info->is_systemop_started = false;
#endif
    }
#if !defined (NDEBUG)
  assert (catalog_access_info->is_systemop_started == false);
#endif

  catalog_access_info->access_started = false;
  catalog_access_info->is_update = false;
  catalog_access_info->need_unlock = false;

  if (catalog_access_info->class_name != NULL && catalog_access_info->need_free_class_name == true)
    {
      free_and_init (catalog_access_info->class_name);
    }
  catalog_access_info->need_free_class_name = false;

  return NO_ERROR;
}
