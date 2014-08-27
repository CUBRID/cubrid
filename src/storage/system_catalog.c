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
#include "btree_load.h"
#include "heap_file.h"
#include "storage_common.h"
#include "xserver_interface.h"
#include "statistics_sr.h"
#include "partition.h"

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

#define CATALOG_MAX_REPR_COUNT \
  (CEIL_PTVDIV(catalog_Max_record_size, CATALOG_REPR_ITEM_SIZE) - 1)

#define CATALOG_PGHEADER_OVFL_PGID_PAGEID_OFF  0
#define CATALOG_PGHEADER_OVFL_PGID_VOLID_OFF   4
#define CATALOG_PGHEADER_DIR_CNT_OFF           8
#define CATALOG_PGHEADER_PG_OVFL_OFF           12

#define CATALOG_PAGE_HEADER_SIZE                  16

/* READERS for CATALOG_PAGE_HEADER related fields */
#define CATALOG_GET_PGHEADER_OVFL_PGID_PAGEID(ptr) \
   (PAGEID) OR_GET_INT((ptr) + CATALOG_PGHEADER_OVFL_PGID_PAGEID_OFF )

#define CATALOG_GET_PGHEADER_OVFL_PGID_VOLID(ptr) \
  (VOLID) OR_GET_SHORT((ptr) + CATALOG_PGHEADER_OVFL_PGID_VOLID_OFF )

#define CATALOG_GET_PGHEADER_DIR_COUNT(ptr) \
   (int) OR_GET_INT((ptr) + CATALOG_PGHEADER_DIR_CNT_OFF )

#define CATALOG_GET_PGHEADER_PG_OVFL(ptr) \
   (bool) OR_GET_INT((ptr) + CATALOG_PGHEADER_PG_OVFL_OFF )

/* WRITERS for CATALOG_PAGE_HEADER related fields */
#define CATALOG_PUT_PGHEADER_OVFL_PGID_PAGEID(ptr,val) \
  OR_PUT_INT((ptr) + CATALOG_PGHEADER_OVFL_PGID_PAGEID_OFF, val)

#define CATALOG_PUT_PGHEADER_OVFL_PGID_VOLID(ptr,val) \
  OR_PUT_SHORT((ptr) + CATALOG_PGHEADER_OVFL_PGID_VOLID_OFF, val)

#define CATALOG_PUT_PGHEADER_DIR_COUNT(ptr,val) \
  OR_PUT_INT((ptr) + CATALOG_PGHEADER_DIR_CNT_OFF, val)

#define CATALOG_PUT_PGHEADER_PG_OVFL(ptr,val) \
  OR_PUT_INT((ptr) + CATALOG_PGHEADER_PG_OVFL_OFF, (int)(val))

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
    OR_GET_BTID((ptr) + CATALOG_BT_STATS_BTID_OFF, var)

#define CATALOG_CLS_INFO_HFID_OFF           0
#define CATALOG_CLS_INFO_TOT_PAGES_OFF     12
#define CATALOG_CLS_INFO_TOT_OBJS_OFF      16
#define CATALOG_CLS_INFO_TIME_STAMP_OFF    20
#define CATALOG_CLS_INFO_SIZE              56
#define CATALOG_CLS_INFO_RESERVED          32

#define CATALOG_REPR_ITEM_PAGEID_PAGEID_OFF   0
#define CATALOG_REPR_ITEM_PAGEID_VOLID_OFF    4
#define CATALOG_REPR_ITEM_REPRID_OFF          8
#define CATALOG_REPR_ITEM_SLOTID_OFF         10

#define CATALOG_REPR_ITEM_SIZE               16

#define CATALOG_GET_REPR_ITEM_PAGEID_PAGEID(ptr)\
  (PAGEID) OR_GET_INT((ptr) + CATALOG_REPR_ITEM_PAGEID_PAGEID_OFF)

#define CATALOG_GET_REPR_ITEM_PAGEID_VOLID(ptr)\
  (VOLID) OR_GET_SHORT((ptr) + CATALOG_REPR_ITEM_PAGEID_VOLID_OFF)

#define CATALOG_GET_REPR_ITEM_REPRID(ptr) \
  (REPR_ID) OR_GET_SHORT((ptr)+ CATALOG_REPR_ITEM_REPRID_OFF)

#define CATALOG_GET_REPR_ITEM_SLOTID(ptr) \
  (PGSLOTID) OR_GET_SHORT((ptr)+ CATALOG_REPR_ITEM_SLOTID_OFF)

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
  PAGEID page_id;
  VOLID volid;
  PGSLOTID slot_id;
  REPR_ID repr_id;
};				/* class identifier + representation identifier */

/* catalog value for the hash table */
typedef struct catalog_value CATALOG_VALUE;
struct catalog_value
{
  VPID page_id;			/* location of representation */
  PGSLOTID slot_id;
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

CTID catalog_Id;		/* global catalog identifier */
static PGLENGTH catalog_Max_record_size;	/* Maximum Record Size */

/*
 * Note: Catalog memory hash table operations are NOT done in CRITICAL
 * SECTIONS, because there can not be simultaneous updaters and readers
 * for the same class representation information.
 */
static MHT_TABLE *catalog_Hash_table = NULL;	/* Catalog memory hash table */
static pthread_mutex_t catalog_Hash_table_lock = PTHREAD_MUTEX_INITIALIZER;

static CATALOG_KEY catalog_Keys[CATALOG_KEY_VALUE_ARRAY_SIZE];	/* array of catalog keys */
static CATALOG_VALUE catalog_Values[CATALOG_KEY_VALUE_ARRAY_SIZE];	/* array of catalog values */
static int catalog_key_value_entry_point;	/* entry point in the key and val arrays */

static CATALOG_MAX_SPACE catalog_Max_space;	/* Global space information */
static pthread_mutex_t catalog_Max_space_lock = PTHREAD_MUTEX_INITIALIZER;
static bool catalog_is_header_initialized = false;

static void catalog_initialize_max_space (CATALOG_MAX_SPACE * header_p);
static void catalog_update_max_space (VPID * page_id, PGLENGTH space);

static bool catalog_initialize_new_page (THREAD_ENTRY * thread_p,
					 const VFID * vfid,
					 const FILE_TYPE file_type,
					 const VPID * vpid,
					 DKNPAGES ignore_npages,
					 void *is_overflow_page);
static PAGE_PTR catalog_get_new_page (THREAD_ENTRY * thread_p, VPID * page_id,
				      VPID * nearpg, bool is_overflow_page);
static PAGE_PTR catalog_find_optimal_page (THREAD_ENTRY * thread_p, int size,
					   VPID * page_id);
static int catalog_get_key_list (THREAD_ENTRY * thread_p, void *key,
				 void *val, void *args);
static void catalog_free_key_list (CATALOG_CLASS_ID_LIST * clsid_list);
static int catalog_compare (const void *key1, const void *key2);
static unsigned int catalog_hash (const void *key, unsigned int htsize);
static int catalog_put_record_into_page (THREAD_ENTRY * thread_p,
					 CATALOG_RECORD * ct_recordp,
					 int next,
					 PGSLOTID * remembered_slotid);
static int catalog_store_disk_representation (THREAD_ENTRY * thread_p,
					      DISK_REPR * disk_reprp,
					      CATALOG_RECORD * ct_recordp,
					      PGSLOTID * remembered_slotid);
static int catalog_store_disk_attribute (THREAD_ENTRY * thread_p,
					 DISK_ATTR * disk_attrp,
					 CATALOG_RECORD * ct_recordp,
					 PGSLOTID * remembered_slotid);
static int catalog_store_attribute_value (THREAD_ENTRY * thread_p,
					  void *value, int length,
					  CATALOG_RECORD * ct_recordp,
					  PGSLOTID * remembered_slotid);
static int catalog_store_btree_statistics (THREAD_ENTRY * thread_p,
					   BTREE_STATS * bt_statsp,
					   CATALOG_RECORD * ct_recordp,
					   PGSLOTID * remembered_slotid);
static int catalog_get_record_from_page (THREAD_ENTRY * thread_p,
					 CATALOG_RECORD * ct_recordp);
static int catalog_fetch_disk_representation (THREAD_ENTRY * thread_p,
					      DISK_REPR * disk_reprp,
					      CATALOG_RECORD * ct_recordp);
static int catalog_fetch_disk_attribute (THREAD_ENTRY * thread_p,
					 DISK_ATTR * disk_attrp,
					 CATALOG_RECORD * ct_recordp);
static int catalog_fetch_attribute_value (THREAD_ENTRY * thread_p,
					  void *value, int length,
					  CATALOG_RECORD * ct_recordp);
static int catalog_fetch_btree_statistics (THREAD_ENTRY * thread_p,
					   BTREE_STATS * bt_statsp,
					   CATALOG_RECORD * ct_recordp);
static int catalog_drop_disk_representation_from_page (THREAD_ENTRY *
						       thread_p,
						       VPID * page_id,
						       PGSLOTID slot_id);
static int catalog_drop_representation_class_from_page (THREAD_ENTRY *
							thread_p,
							VPID * dir_pgid,
							PAGE_PTR * dir_pgptr,
							VPID * page_id,
							PGSLOTID slot_id);
static PAGE_PTR catalog_get_representation_record (THREAD_ENTRY * thread_p,
						   OID * oid_p,
						   RECDES * record_p,
						   int latch, int is_peek,
						   int *out_repr_count_p);
static PAGE_PTR catalog_get_representation_record_after_search (THREAD_ENTRY *
								thread_p,
								OID *
								class_id_p,
								RECDES *
								record_p,
								int latch,
								int is_peek,
								OID * oid_p,
								int
								*out_repr_count_p);
static int catalog_adjust_directory_count (THREAD_ENTRY * thread_p,
					   PAGE_PTR page_p, RECDES * record_p,
					   int delta);
static void catalog_delete_key (OID * class_id_p, REPR_ID repr_id);
static char *catalog_find_representation_item_position (INT16 repr_id,
							int repr_cnt,
							char *repr_p,
							int *out_position_p);
static int catalog_insert_representation_item (THREAD_ENTRY * thread_p,
					       RECDES * record_p, void *key);
static int catalog_drop_directory (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				   RECDES * record_p, OID * oid_p,
				   OID * class_id_p);
static void catalog_copy_btree_statistic (BTREE_STATS * new_btree_stats_p,
					  int new_btree_stats_count,
					  BTREE_STATS * pre_btree_stats_p,
					  int pre_btree_stats_count);
static void catalog_copy_disk_attributes (DISK_ATTR * new_attrs_p,
					  int new_attr_count,
					  DISK_ATTR * pre_attrs_p,
					  int pre_attr_count);
static int catalog_sum_disk_attribute_size (DISK_ATTR * attrs_p, int count);

static int catalog_put_representation_item (THREAD_ENTRY * thread_p,
					    OID * class_id,
					    CATALOG_REPR_ITEM * repr_item);
static int catalog_get_representation_item (THREAD_ENTRY * thread_p,
					    OID * class_id,
					    CATALOG_REPR_ITEM * repr_item);
static int catalog_drop_representation_item (THREAD_ENTRY * thread_p,
					     OID * class_id,
					     CATALOG_REPR_ITEM * repr_item);
static int catalog_drop (THREAD_ENTRY * thread_p, OID * class_id,
			 REPR_ID repr_id);
static int catalog_drop_all (THREAD_ENTRY * thread_p, OID * class_id);
static int catalog_drop_all_representation_and_class (THREAD_ENTRY * thread_p,
						      OID * class_id);
static int catalog_fixup_missing_disk_representation (THREAD_ENTRY * thread_p,
						      OID * class_oid,
						      REPR_ID reprid);
static int catalog_fixup_missing_class_info (THREAD_ENTRY * thread_p,
					     OID * class_oid);
static int catalog_print_entry (THREAD_ENTRY * thread_p, void *key, void *val,
				void *args);
static DISK_ISVALID catalog_check_class_consistency (THREAD_ENTRY * thread_p,
						     OID * class_oid);
static int catalog_check_class (THREAD_ENTRY * thread_p, void *key, void *val,
				void *args);
static void catalog_dump_disk_attribute (DISK_ATTR * atr);
static void catalog_dump_representation (DISK_REPR * dr);
static void catalog_clear_hash_table ();

static void catalog_put_page_header (char *rec_p,
				     CATALOG_PAGE_HEADER * header_p);
static void catalog_get_disk_representation (DISK_REPR * disk_repr_p,
					     char *rec_p);
static void catalog_put_disk_representation (char *rec_p,
					     DISK_REPR * disk_repr_p);
static void catalog_get_disk_attribute (DISK_ATTR * attr_p, char *rec_p);
static void catalog_put_disk_attribute (char *rec_p, DISK_ATTR * attr_p);
static void catalog_put_btree_statistics (char *rec_p, BTREE_STATS * stat_p);
static void catalog_get_class_info_from_record (CLS_INFO * class_info_p,
						char *rec_p);
static void catalog_put_class_info_to_record (char *rec_p,
					      CLS_INFO * class_info_p);
static void catalog_get_repr_item_from_record (CATALOG_REPR_ITEM * item_p,
					       char *rec_p);
static void catalog_put_repr_item_to_record (char *rec_p,
					     CATALOG_REPR_ITEM * item_p);
static int catalog_assign_attribute (THREAD_ENTRY * thread_p,
				     DISK_ATTR * disk_attr_p,
				     CATALOG_RECORD * catalog_record_p);

static void
catalog_put_page_header (char *rec_p, CATALOG_PAGE_HEADER * header_p)
{
  CATALOG_PUT_PGHEADER_OVFL_PGID_PAGEID (rec_p,
					 header_p->overflow_page_id.pageid);
  CATALOG_PUT_PGHEADER_OVFL_PGID_VOLID (rec_p,
					header_p->overflow_page_id.volid);
  CATALOG_PUT_PGHEADER_DIR_COUNT (rec_p, header_p->dir_count);
  CATALOG_PUT_PGHEADER_PG_OVFL (rec_p, header_p->is_overflow_page);
}

static void
catalog_get_disk_representation (DISK_REPR * disk_repr_p, char *rec_p)
{
  disk_repr_p->id = (REPR_ID) OR_GET_INT (rec_p + CATALOG_DISK_REPR_ID_OFF);
  disk_repr_p->n_fixed = OR_GET_INT (rec_p + CATALOG_DISK_REPR_N_FIXED_OFF);
  disk_repr_p->fixed = NULL;
  disk_repr_p->fixed_length =
    OR_GET_INT (rec_p + CATALOG_DISK_REPR_FIXED_LENGTH_OFF);
  disk_repr_p->n_variable =
    OR_GET_INT (rec_p + CATALOG_DISK_REPR_N_VARIABLE_OFF);
  disk_repr_p->variable = NULL;

#if 0				/* reserved for future use */
  disk_repr_p->repr_reserved_1 =
    OR_GET_INT (rec_p + CATALOG_DISK_REPR_RESERVED_1_OFF);
#endif
}

static void
catalog_put_disk_representation (char *rec_p, DISK_REPR * disk_repr_p)
{
  OR_PUT_INT (rec_p + CATALOG_DISK_REPR_ID_OFF, disk_repr_p->id);
  OR_PUT_INT (rec_p + CATALOG_DISK_REPR_N_FIXED_OFF, disk_repr_p->n_fixed);
  OR_PUT_INT (rec_p + CATALOG_DISK_REPR_FIXED_LENGTH_OFF,
	      disk_repr_p->fixed_length);
  OR_PUT_INT (rec_p + CATALOG_DISK_REPR_N_VARIABLE_OFF,
	      disk_repr_p->n_variable);

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
      stat_p->pkeys[i] = OR_GET_INT (rec_p + CATALOG_BT_STATS_PKEYS_OFF +
				     (OR_INT_SIZE * i));
    }
#if 0				/* reserved for future use */
  for (i = 0; i < BTREE_STATS_RESERVED_NUM; i++)
    {
      stat_p->reserved[i] =
	OR_GET_INT (rec_p + CATALOG_BT_STATS_RESERVED_OFF +
		    (OR_INT_SIZE * i));
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
      OR_PUT_INT (rec_p + CATALOG_BT_STATS_PKEYS_OFF + (OR_INT_SIZE * i),
		  stat_p->pkeys[i]);
    }

#if 1				/* reserved for future use */
  for (i = 0; i < BTREE_STATS_RESERVED_NUM; i++)
    {
      OR_PUT_INT (rec_p + CATALOG_BT_STATS_RESERVED_OFF + (OR_INT_SIZE * i),
		  0);
    }
#endif
}

static void
catalog_get_class_info_from_record (CLS_INFO * class_info_p, char *rec_p)
{
  OR_GET_HFID (rec_p + CATALOG_CLS_INFO_HFID_OFF, &class_info_p->hfid);
  class_info_p->tot_pages =
    OR_GET_INT (rec_p + CATALOG_CLS_INFO_TOT_PAGES_OFF);
  class_info_p->tot_objects =
    OR_GET_INT (rec_p + CATALOG_CLS_INFO_TOT_OBJS_OFF);
  class_info_p->time_stamp =
    OR_GET_INT (rec_p + CATALOG_CLS_INFO_TIME_STAMP_OFF);
}

static void
catalog_put_class_info_to_record (char *rec_p, CLS_INFO * class_info_p)
{
  OR_PUT_HFID (rec_p + CATALOG_CLS_INFO_HFID_OFF, &class_info_p->hfid);
  OR_PUT_INT (rec_p + CATALOG_CLS_INFO_TOT_PAGES_OFF,
	      class_info_p->tot_pages);
  OR_PUT_INT (rec_p + CATALOG_CLS_INFO_TOT_OBJS_OFF,
	      class_info_p->tot_objects);
  OR_PUT_INT (rec_p + CATALOG_CLS_INFO_TIME_STAMP_OFF,
	      class_info_p->time_stamp);
}

static void
catalog_get_repr_item_from_record (CATALOG_REPR_ITEM * item_p, char *rec_p)
{
  item_p->page_id.pageid =
    OR_GET_INT (rec_p + CATALOG_REPR_ITEM_PAGEID_PAGEID_OFF);
  item_p->page_id.volid =
    OR_GET_SHORT (rec_p + CATALOG_REPR_ITEM_PAGEID_VOLID_OFF);
  item_p->repr_id = OR_GET_SHORT (rec_p + CATALOG_REPR_ITEM_REPRID_OFF);
  item_p->slot_id = OR_GET_SHORT (rec_p + CATALOG_REPR_ITEM_SLOTID_OFF);
}

static void
catalog_put_repr_item_to_record (char *rec_p, CATALOG_REPR_ITEM * item_p)
{
  OR_PUT_INT (rec_p + CATALOG_REPR_ITEM_PAGEID_PAGEID_OFF,
	      item_p->page_id.pageid);
  OR_PUT_SHORT (rec_p + CATALOG_REPR_ITEM_PAGEID_VOLID_OFF,
		item_p->page_id.volid);
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
static bool
catalog_initialize_new_page (THREAD_ENTRY * thread_p, const VFID * vfid_p,
			     const FILE_TYPE file_type, const VPID * vpid_p,
			     DKNPAGES ignore_npages, void *is_overflow_page)
{
  PAGE_PTR page_p;
  CATALOG_PAGE_HEADER page_header;
  PGSLOTID slot_id;
  int success;
  RECDES record = {
    CATALOG_PAGE_HEADER_SIZE, CATALOG_PAGE_HEADER_SIZE, REC_HOME, NULL
  };
  char data[CATALOG_PAGE_HEADER_SIZE + MAX_ALIGNMENT], *aligned_data;

  aligned_data = PTR_ALIGN (data, MAX_ALIGNMENT);

  page_p = pgbuf_fix (thread_p, vpid_p, NEW_PAGE, PGBUF_LATCH_WRITE,
		      PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      return false;
    }

  (void) pgbuf_set_page_ptype (thread_p, page_p, PAGE_CATALOG);

  spage_initialize (thread_p, page_p, ANCHORED_DONT_REUSE_SLOTS,
		    MAX_ALIGNMENT, SAFEGUARD_RVSPACE);

  VPID_SET_NULL (&page_header.overflow_page_id);
  page_header.dir_count = 0;
  page_header.is_overflow_page = (bool) is_overflow_page;

  recdes_set_data_area (&record, aligned_data, CATALOG_PAGE_HEADER_SIZE);
  catalog_put_page_header (record.data, &page_header);

  success = spage_insert (thread_p, page_p, &record, &slot_id);
  if (success != SP_SUCCESS || slot_id != CATALOG_HEADER_SLOT)
    {
      if (success != SP_SUCCESS)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}

      pgbuf_unfix_and_init (thread_p, page_p);
      return false;
    }

  log_append_redo_data2 (thread_p, RVCT_NEWPAGE, vfid_p, page_p, -1,
			 sizeof (CATALOG_PAGE_HEADER), &page_header);

  pgbuf_set_dirty (thread_p, page_p, FREE);
  return true;
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
catalog_get_new_page (THREAD_ENTRY * thread_p, VPID * page_id_p,
		      VPID * near_page_p, bool is_overflow_page)
{
  PAGE_PTR page_p;

  if (file_alloc_pages (thread_p, &catalog_Id.vfid, page_id_p, 1, near_page_p,
			catalog_initialize_new_page,
			(void *) is_overflow_page) == NULL)
    {
      return NULL;
    }

  /*
   * Note: we fetch the page as old since it was initialized during the
   * allocation by catalog_initialize_new_page, we want the current
   * contents of the page.
   */

  page_p = pgbuf_fix (thread_p, page_id_p, OLD_PAGE, PGBUF_LATCH_WRITE,
		      PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      (void) file_dealloc_page (thread_p, &catalog_Id.vfid, page_id_p);
    }

  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  return page_p;
}

/*
 * catalog_find_optimal_page () -
 *   return: PAGE_PTR
 *   size(in): The size requested in the page
 *   page_id(out): Set to the page identifier fetched
 *
 * Note: The routine considers the requested size and hinted catalog
 * space information to find a catalog page optimal for the good
 * catalog storage utilization and returns the page.
 */
static PAGE_PTR
catalog_find_optimal_page (THREAD_ENTRY * thread_p, int size,
			   VPID * page_id_p)
{
  PAGE_PTR page_p;
  VPID near_vpid;
  PAGEID overflow_page_id;
  char data[CATALOG_PAGE_HEADER_SIZE + MAX_ALIGNMENT], *aligned_data;
  RECDES record = {
    CATALOG_PAGE_HEADER_SIZE, CATALOG_PAGE_HEADER_SIZE, REC_HOME, NULL
  };
  int dir_count;
  int free_space;
  float empty_ratio;
  int page_count;
  bool is_overflow_page;
  int rv;

  aligned_data = PTR_ALIGN (data, MAX_ALIGNMENT);

  rv = pthread_mutex_lock (&catalog_Max_space_lock);
  recdes_set_data_area (&record, aligned_data, CATALOG_PAGE_HEADER_SIZE);

  if (catalog_Max_space.max_page_id.pageid == NULL_PAGEID)
    {
      /* if catalog has pages, make hinted space information point to the
       * last catalog page, this way, during restarts the last page of the
       * previous run will not be left around empty.
       */
      page_count = file_get_numpages (thread_p, &catalog_Id.vfid);
      if ((page_count > 0)
	  && (file_find_nthpages (thread_p, &catalog_Id.vfid,
				  &catalog_Max_space.max_page_id,
				  (page_count - 1), 1) == 1)
	  && catalog_Max_space.max_page_id.pageid != NULL_PAGEID)
	{
	  page_p = pgbuf_fix (thread_p, &catalog_Max_space.max_page_id,
			      OLD_PAGE, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
	  if (page_p == NULL)
	    {
	      pthread_mutex_unlock (&catalog_Max_space_lock);
	      return NULL;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

	  if (spage_get_record (page_p, CATALOG_HEADER_SLOT, &record, COPY) !=
	      S_SUCCESS)
	    {
	      pgbuf_unfix_and_init (thread_p, page_p);
	      pthread_mutex_unlock (&catalog_Max_space_lock);
	      return NULL;
	    }

	  is_overflow_page = CATALOG_GET_PGHEADER_PG_OVFL (record.data);
	  if (is_overflow_page)
	    {
	      VPID_SET_NULL (&catalog_Max_space.max_page_id);
	    }
	  else
	    {
	      catalog_Max_space.max_space =
		spage_max_space_for_new_record (thread_p, page_p);
	    }
	  pgbuf_unfix_and_init (thread_p, page_p);
	}
    }

  near_vpid.volid = catalog_Id.vfid.volid;
  near_vpid.pageid = catalog_Id.hpgid;

  if (catalog_Max_space.max_page_id.pageid == NULL_PAGEID
      || size > catalog_Max_space.max_space)
    {
      pthread_mutex_unlock (&catalog_Max_space_lock);
      return catalog_get_new_page (thread_p, page_id_p, &near_vpid, false);
    }
  else
    {
      *page_id_p = catalog_Max_space.max_page_id;
      pthread_mutex_unlock (&catalog_Max_space_lock);

      page_p = pgbuf_fix (thread_p, page_id_p, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
      if (page_p == NULL)
	{
	  return NULL;
	}

      (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

      /* if the page is an overflow page or has itself an overflow page,
       * it can only have one record and cannot be used for other purposes,
       * so reject to use same page.
       */
      if (spage_get_record (page_p, CATALOG_HEADER_SLOT, &record, COPY) !=
	  S_SUCCESS)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  return NULL;
	}

      overflow_page_id = CATALOG_GET_PGHEADER_OVFL_PGID_PAGEID (record.data);
      is_overflow_page = CATALOG_GET_PGHEADER_PG_OVFL (record.data);
      if (overflow_page_id != NULL_PAGEID || is_overflow_page)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  return catalog_get_new_page (thread_p, page_id_p, &near_vpid,
				       false);
	}

      /* if the page contains directories leave enough space for
       * expansion of directories in order not to cause them to
       * move around.
       */
      free_space =
	spage_max_space_for_new_record (thread_p,
					page_p) - CATALOG_MAX_SLOT_ID_SIZE;
      dir_count = CATALOG_GET_PGHEADER_DIR_COUNT (record.data);
      if (dir_count > 0)
	{
	  empty_ratio =
	    dir_count <= 0 ? 0.0f : 0.25f + (dir_count - 1) * 0.05f;
	  if (free_space <= (DB_PAGESIZE * empty_ratio))
	    {
	      /* page needs to be left empty */
	      pgbuf_unfix_and_init (thread_p, page_p);
	      return catalog_get_new_page (thread_p, page_id_p, &near_vpid,
					   false);
	    }
	}

      if (size >= free_space)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  return catalog_get_new_page (thread_p, page_id_p, &near_vpid,
				       false);
	}

      return page_p;
    }
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
	  attr_p = (k < repr_p->n_fixed) ? (DISK_ATTR *) repr_p->fixed + k
	    : (DISK_ATTR *) repr_p->variable + (k - repr_p->n_fixed);

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
catalog_get_key_list (THREAD_ENTRY * thread_p, void *key, void *ignore_value,
		      void *args)
{
  CATALOG_CLASS_ID_LIST *class_id_p, **p;

  p = (CATALOG_CLASS_ID_LIST **) args;

  class_id_p = (CATALOG_CLASS_ID_LIST *)
    db_private_alloc (thread_p, sizeof (CATALOG_CLASS_ID_LIST));

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
 * catalog_compare () - Compare two catalog keys
 *   return: int (true or false)
 *   key1(in): First catalog key
 *   key2(in): Second catalog key
 */
static int
catalog_compare (const void *key1, const void *key2)
{
  const CATALOG_KEY *k1, *k2;

  k1 = (const CATALOG_KEY *) key1;
  k2 = (const CATALOG_KEY *) key2;

  if (k1->page_id == k2->page_id && k1->slot_id == k2->slot_id
      && k1->repr_id == k2->repr_id && k1->volid == k2->volid)
    {
      return true;
    }
  else
    {
      return false;
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
catalog_hash (const void *key, unsigned int hash_table_size)
{
  const CATALOG_KEY *k1 = (const CATALOG_KEY *) key;
  unsigned int hash_res;

  hash_res = ((((k1)->slot_id | ((k1)->page_id << 8)) ^
	       (((k1)->page_id >> 8) | (((PAGEID) (k1)->volid) << 24))) +
	      k1->repr_id);

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
catalog_put_record_into_page (THREAD_ENTRY * thread_p,
			      CATALOG_RECORD * catalog_record_p, int next,
			      PGSLOTID * remembered_slot_id_p)
{
  PAGE_PTR new_page_p;
  VPID new_vpid;

  /* if some space in 'recdes'data' is remained */
  if (catalog_record_p->offset < catalog_record_p->recdes.area_size)
    {
      return NO_ERROR;
    }

  if (spage_insert (thread_p, catalog_record_p->page_p,
		    &catalog_record_p->recdes,
		    &catalog_record_p->slotid) != SP_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, catalog_record_p->page_p);
      return ER_FAILED;
    }

  if (*remembered_slot_id_p == NULL_SLOTID)
    {
      *remembered_slot_id_p = catalog_record_p->slotid;
    }

  log_append_undoredo_recdes2 (thread_p, RVCT_INSERT, &catalog_Id.vfid,
			       catalog_record_p->page_p,
			       catalog_record_p->slotid, NULL,
			       &catalog_record_p->recdes);
  pgbuf_set_dirty (thread_p, catalog_record_p->page_p, DONT_FREE);

  /* if there's no need to get next page; when this is the last page */
  if (!next)
    {
      catalog_record_p->slotid = *remembered_slot_id_p;
      *remembered_slot_id_p = NULL_SLOTID;
      return NO_ERROR;
    }

  new_page_p = catalog_get_new_page (thread_p, &new_vpid,
				     &catalog_record_p->vpid, true);
  if (new_page_p == NULL)
    {
      pgbuf_unfix_and_init (thread_p, catalog_record_p->page_p);
      return ER_FAILED;
    }

  log_append_undo_data2 (thread_p, RVCT_NEW_OVFPAGE_LOGICAL_UNDO,
			 &catalog_Id.vfid, NULL, -1, sizeof (new_vpid),
			 &new_vpid);

  /* make the previous page point to the newly allocated page */
  catalog_record_p->recdes.area_size = DB_PAGESIZE;
  (void) spage_get_record (catalog_record_p->page_p, CATALOG_HEADER_SLOT,
			   &catalog_record_p->recdes, COPY);

  log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid,
			   catalog_record_p->page_p, CATALOG_HEADER_SLOT,
			   &catalog_record_p->recdes);

  CATALOG_PUT_PGHEADER_OVFL_PGID_PAGEID (catalog_record_p->recdes.data,
					 new_vpid.pageid);
  CATALOG_PUT_PGHEADER_OVFL_PGID_VOLID (catalog_record_p->recdes.data,
					new_vpid.volid);

  if (spage_update (thread_p, catalog_record_p->page_p, CATALOG_HEADER_SLOT,
		    &catalog_record_p->recdes) != SP_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, catalog_record_p->page_p);
      return ER_FAILED;
    }

  log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid,
			   catalog_record_p->page_p, CATALOG_HEADER_SLOT,
			   &catalog_record_p->recdes);

  pgbuf_set_dirty (thread_p, catalog_record_p->page_p, FREE);

  catalog_record_p->vpid.pageid = new_vpid.pageid;
  catalog_record_p->vpid.volid = new_vpid.volid;
  catalog_record_p->page_p = new_page_p;
  catalog_record_p->recdes.area_size =
    spage_max_space_for_new_record (thread_p,
				    new_page_p) - CATALOG_MAX_SLOT_ID_SIZE;
  catalog_record_p->recdes.length = 0;
  catalog_record_p->offset = 0;

  return NO_ERROR;
}

static int
catalog_write_unwritten_portion (THREAD_ENTRY * thread_p,
				 CATALOG_RECORD * catalog_record_p,
				 PGSLOTID * remembered_slot_id_p,
				 int format_size)
{
  /* if the remained, unwritten portion of the record data is smaller than the
     size of disk format of structure */
  if (catalog_record_p->recdes.area_size - catalog_record_p->offset <
      format_size)
    {
      /* set the record length as the current offset, the size of written
         portion of the record data, and set the offset to the end of the
         record data to write the page */
      catalog_record_p->recdes.length = catalog_record_p->offset;
      catalog_record_p->offset = catalog_record_p->recdes.area_size;

      if (catalog_put_record_into_page (thread_p, catalog_record_p, 1,
					remembered_slot_id_p) != NO_ERROR)
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
catalog_store_disk_representation (THREAD_ENTRY * thread_p,
				   DISK_REPR * disk_repr_p,
				   CATALOG_RECORD * catalog_record_p,
				   PGSLOTID * remembered_slot_id_p)
{
  if (catalog_write_unwritten_portion (thread_p, catalog_record_p,
				       remembered_slot_id_p,
				       CATALOG_DISK_REPR_SIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  catalog_put_disk_representation (catalog_record_p->recdes.data +
				   catalog_record_p->offset, disk_repr_p);
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
catalog_store_disk_attribute (THREAD_ENTRY * thread_p,
			      DISK_ATTR * disk_attr_p,
			      CATALOG_RECORD * catalog_record_p,
			      PGSLOTID * remembered_slot_id_p)
{
  if (catalog_write_unwritten_portion (thread_p, catalog_record_p,
				       remembered_slot_id_p,
				       CATALOG_DISK_ATTR_SIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  catalog_put_disk_attribute (catalog_record_p->recdes.data +
			      catalog_record_p->offset, disk_attr_p);
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
catalog_store_attribute_value (THREAD_ENTRY * thread_p, void *value,
			       int length, CATALOG_RECORD * catalog_record_p,
			       PGSLOTID * remembered_slot_id_p)
{
  int offset = 0;
  int bufsize;

  if (catalog_write_unwritten_portion (thread_p, catalog_record_p,
				       remembered_slot_id_p,
				       length) != NO_ERROR)
    {
      return ER_FAILED;
    }

  while (offset < length)
    {
      if (length - offset <=
	  catalog_record_p->recdes.area_size - catalog_record_p->offset)
	{
	  /* if the size of the value is smaller than or equals to the
	   * remaining size of the recdes.data, just copy the value into the
	   * recdes.data buffer and adjust the offset.
	   */
	  bufsize = length - offset;
	  (void) memcpy (catalog_record_p->recdes.data +
			 catalog_record_p->offset, (char *) value + offset,
			 bufsize);
	  catalog_record_p->offset += bufsize;
	  break;
	}
      else
	{
	  /* if the size of the value is larger than the whole size of the
	   * recdes.data, we need split the value over N pages. The first N-1
	   * pages need to be stored into pages, while the last page can be
	   * stored in the recdes.data buffer as the existing routine.
	   */
	  assert (catalog_record_p->offset == 0);
	  bufsize = catalog_record_p->recdes.area_size;
	  (void) memcpy (catalog_record_p->recdes.data,
			 (char *) value + offset, bufsize);
	  offset += bufsize;

	  /* write recdes.data and fill catalog_record_p as new page */
	  catalog_record_p->offset = catalog_record_p->recdes.area_size;
	  catalog_record_p->recdes.length = catalog_record_p->offset;
	  if (catalog_put_record_into_page (thread_p, catalog_record_p, 1,
					    remembered_slot_id_p) != NO_ERROR)
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
catalog_store_btree_statistics (THREAD_ENTRY * thread_p,
				BTREE_STATS * btree_stats_p,
				CATALOG_RECORD * catalog_record_p,
				PGSLOTID * remembered_slot_id_p)
{
  if (catalog_write_unwritten_portion (thread_p, catalog_record_p,
				       remembered_slot_id_p,
				       CATALOG_BT_STATS_SIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  catalog_put_btree_statistics (catalog_record_p->recdes.data +
				catalog_record_p->offset, btree_stats_p);
  catalog_record_p->offset += CATALOG_BT_STATS_SIZE;

  return NO_ERROR;
}

/*
 * catalog_get_record_from_page () - Get the catalog record from the page.
 *   return: NO_ERROR or ER_FAILED
 *   ct_recordp(in): pointer to CATALOG_RECORD structure
 */
static int
catalog_get_record_from_page (THREAD_ENTRY * thread_p,
			      CATALOG_RECORD * catalog_record_p)
{
  /* if some data in 'recdes.data' is remained */
  if (catalog_record_p->offset < catalog_record_p->recdes.length)
    {
      return NO_ERROR;
    }

  /* if it is not first time, if there was the page previously read */
  if (catalog_record_p->page_p)
    {
      if (spage_get_record (catalog_record_p->page_p, CATALOG_HEADER_SLOT,
			    &catalog_record_p->recdes, PEEK) != S_SUCCESS)
	{
	  return ER_FAILED;
	}

      catalog_record_p->vpid.pageid =
	CATALOG_GET_PGHEADER_OVFL_PGID_PAGEID (catalog_record_p->recdes.data);
      catalog_record_p->vpid.volid =
	CATALOG_GET_PGHEADER_OVFL_PGID_VOLID (catalog_record_p->recdes.data);
      catalog_record_p->slotid = 1;

      pgbuf_unfix_and_init (thread_p, catalog_record_p->page_p);
    }

  if (catalog_record_p->vpid.pageid == NULL_PAGEID ||
      catalog_record_p->vpid.volid == NULL_VOLID)
    {
      return ER_FAILED;
    }

  catalog_record_p->page_p = pgbuf_fix (thread_p, &catalog_record_p->vpid,
					OLD_PAGE, PGBUF_LATCH_READ,
					PGBUF_UNCONDITIONAL_LATCH);
  if (catalog_record_p->page_p == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, catalog_record_p->page_p,
				 PAGE_CATALOG);

  if (spage_get_record (catalog_record_p->page_p, catalog_record_p->slotid,
			&catalog_record_p->recdes, PEEK) != S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, catalog_record_p->page_p);
      return ER_FAILED;
    }

  catalog_record_p->offset = 0;
  return NO_ERROR;
}

static int
catalog_read_unread_portion (THREAD_ENTRY * thread_p,
			     CATALOG_RECORD * catalog_record_p,
			     int format_size)
{
  if (catalog_record_p->recdes.length - catalog_record_p->offset <
      format_size)
    {
      catalog_record_p->offset = catalog_record_p->recdes.length;
      if (catalog_get_record_from_page (thread_p, catalog_record_p) !=
	  NO_ERROR)
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
catalog_fetch_disk_representation (THREAD_ENTRY * thread_p,
				   DISK_REPR * disk_repr_p,
				   CATALOG_RECORD * catalog_record_p)
{
  if (catalog_read_unread_portion (thread_p, catalog_record_p,
				   CATALOG_DISK_REPR_SIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  catalog_get_disk_representation (disk_repr_p,
				   catalog_record_p->recdes.data +
				   catalog_record_p->offset);
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
catalog_fetch_disk_attribute (THREAD_ENTRY * thread_p,
			      DISK_ATTR * disk_attr_p,
			      CATALOG_RECORD * catalog_record_p)
{
  if (catalog_read_unread_portion (thread_p, catalog_record_p,
				   CATALOG_DISK_ATTR_SIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  catalog_get_disk_attribute (disk_attr_p,
			      catalog_record_p->recdes.data +
			      catalog_record_p->offset);
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
catalog_fetch_attribute_value (THREAD_ENTRY * thread_p, void *value,
			       int length, CATALOG_RECORD * catalog_record_p)
{
  int offset = 0;
  int bufsize;

  if (catalog_read_unread_portion (thread_p, catalog_record_p, length) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  while (offset < length)
    {
      if (length - offset <=
	  catalog_record_p->recdes.length - catalog_record_p->offset)
	{
	  /* if the size of the value is smaller than or equals to the
	   * remaining length of the recdes.data, just read the value from the
	   * recdes.data buffer and adjust the offset.
	   */
	  bufsize = length - offset;
	  (void) memcpy ((char *) value + offset,
			 catalog_record_p->recdes.data +
			 catalog_record_p->offset, bufsize);
	  catalog_record_p->offset += bufsize;
	  break;
	}
      else
	{
	  /* if the size of the value is larger than the whole length of the
	   * recdes.data, that means the value has been stored in N pages, we
	   * need to fetch these N pages and read value from them. in first N-1
	   * page, the whole page will be read into the value buffer, while in
	   * last page, the remaining value will be read into value buffer as
	   * the existing routine.
	   */
	  assert (catalog_record_p->offset == 0);
	  bufsize = catalog_record_p->recdes.length;
	  (void) memcpy ((char *) value + offset,
			 catalog_record_p->recdes.data, bufsize);
	  offset += bufsize;

	  /* read next page and fill the catalog_record_p */
	  catalog_record_p->offset = catalog_record_p->recdes.length;
	  if (catalog_get_record_from_page (thread_p, catalog_record_p) !=
	      NO_ERROR)
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
catalog_fetch_btree_statistics (THREAD_ENTRY * thread_p,
				BTREE_STATS * btree_stats_p,
				CATALOG_RECORD * catalog_record_p)
{
  VPID root_vpid;
  PAGE_PTR root_page_p;
  BTREE_ROOT_HEADER *root_header = NULL;
  int i;
  OR_BUF buf;

  if (catalog_read_unread_portion (thread_p, catalog_record_p,
				   CATALOG_BT_STATS_SIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  btree_stats_p->pkeys_size = 0;
  btree_stats_p->pkeys = NULL;

  CATALOG_GET_BT_STATS_BTID (&btree_stats_p->btid,
			     catalog_record_p->recdes.data +
			     catalog_record_p->offset);

  root_vpid.pageid = btree_stats_p->btid.root_pageid;
  root_vpid.volid = btree_stats_p->btid.vfid.volid;
  if (VPID_ISNULL (&root_vpid))
    {
      /* after create the catalog record of the class, and
       * before create the catalog record of the constraints for the class
       *
       * currently, does not know BTID
       */
      btree_stats_p->key_type = &tp_Null_domain;
      goto exit_on_end;
    }

  root_page_p = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			   PGBUF_UNCONDITIONAL_LATCH);
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
      btree_stats_p->pkeys_size =
	tp_domain_size (btree_stats_p->key_type->setdomain);
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

  btree_stats_p->pkeys =
    (int *) db_private_alloc (thread_p,
			      btree_stats_p->pkeys_size * sizeof (int));
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

  catalog_get_btree_statistics (btree_stats_p,
				catalog_record_p->recdes.data +
				catalog_record_p->offset);
  catalog_record_p->offset += CATALOG_BT_STATS_SIZE;

  return NO_ERROR;
}

static int
catalog_drop_representation_helper (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				    VPID * page_id_p, PGSLOTID slot_id)
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

  log_append_undoredo_recdes2 (thread_p, RVCT_DELETE, &catalog_Id.vfid,
			       page_p, slot_id, &record, NULL);

  if (spage_delete (thread_p, page_p, slot_id) != slot_id)
    {
      recdes_free_data_area (&record);
      return ER_FAILED;
    }

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  catalog_update_max_space (page_id_p, new_space);

  spage_get_record (page_p, CATALOG_HEADER_SLOT, &record, COPY);

  log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p,
			   CATALOG_HEADER_SLOT, &record);

  overflow_vpid.pageid = CATALOG_GET_PGHEADER_OVFL_PGID_PAGEID (record.data);
  overflow_vpid.volid = CATALOG_GET_PGHEADER_OVFL_PGID_VOLID (record.data);

  if (overflow_vpid.pageid != NULL_PAGEID)
    {
      CATALOG_PUT_PGHEADER_OVFL_PGID_PAGEID (record.data, NULL_PAGEID);
      CATALOG_PUT_PGHEADER_OVFL_PGID_VOLID (record.data, NULL_VOLID);

      if (spage_update (thread_p, page_p, CATALOG_HEADER_SLOT, &record) !=
	  SP_SUCCESS)
	{
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}

      log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid,
			       page_p, CATALOG_HEADER_SLOT, &record);
    }

  recdes_free_data_area (&record);

  while (overflow_vpid.pageid != NULL_PAGEID)
    {
      /* delete the records in the overflow pages, if any */
      overflow_page_p = pgbuf_fix (thread_p, &overflow_vpid, OLD_PAGE,
				   PGBUF_LATCH_WRITE,
				   PGBUF_UNCONDITIONAL_LATCH);
      if (overflow_page_p == NULL)
	{
	  return ER_FAILED;
	}

      (void) pgbuf_check_page_ptype (thread_p, overflow_page_p, PAGE_CATALOG);

      spage_get_record (overflow_page_p, CATALOG_HEADER_SLOT, &record, PEEK);
      new_overflow_vpid.pageid =
	CATALOG_GET_PGHEADER_OVFL_PGID_PAGEID (record.data);
      new_overflow_vpid.volid =
	CATALOG_GET_PGHEADER_OVFL_PGID_VOLID (record.data);

      pgbuf_unfix_and_init (thread_p, overflow_page_p);
      file_dealloc_page (thread_p, &catalog_Id.vfid, &overflow_vpid);
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
catalog_drop_disk_representation_from_page (THREAD_ENTRY * thread_p,
					    VPID * page_id_p,
					    PGSLOTID slot_id)
{
  PAGE_PTR page_p;

  page_p = pgbuf_fix (thread_p, page_id_p, OLD_PAGE, PGBUF_LATCH_WRITE,
		      PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  if (catalog_drop_representation_helper (thread_p, page_p, page_id_p,
					  slot_id) != NO_ERROR)
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
catalog_drop_representation_class_from_page (THREAD_ENTRY * thread_p,
					     VPID * dir_page_id_p,
					     PAGE_PTR * dir_page_p,
					     VPID * page_id_p,
					     PGSLOTID slot_id)
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
      page_p = pgbuf_fix (thread_p, page_id_p, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_CONDITIONAL_LATCH);
      if (page_p == NULL)
	{
	  /* try reverse order */
	  pgbuf_unfix_and_init (thread_p, *dir_page_p);

	  page_p =
	    pgbuf_fix (thread_p, page_id_p, OLD_PAGE, PGBUF_LATCH_WRITE,
		       PGBUF_UNCONDITIONAL_LATCH);
	  if (page_p == NULL)
	    {
	      return ER_FAILED;
	    }

	  *dir_page_p = pgbuf_fix (thread_p, dir_page_id_p, OLD_PAGE,
				   PGBUF_LATCH_WRITE,
				   PGBUF_CONDITIONAL_LATCH);
	  if ((*dir_page_p) == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, page_p);

	      *dir_page_p = pgbuf_fix (thread_p, dir_page_id_p, OLD_PAGE,
				       PGBUF_LATCH_WRITE,
				       PGBUF_UNCONDITIONAL_LATCH);
	      if ((*dir_page_p) == NULL)
		{
		  return ER_FAILED;
		}

	      if (again_count++ >= again_max)
		{
		  if (er_errid () == NO_ERROR)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_PAGE_LATCH_ABORTED, 2, page_id_p->volid,
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

  if (catalog_drop_representation_helper (thread_p, page_p, page_id_p,
					  slot_id) != NO_ERROR)
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

static PAGE_PTR
catalog_get_representation_record (THREAD_ENTRY * thread_p, OID * oid_p,
				   RECDES * record_p, int latch, int is_peek,
				   int *out_repr_count_p)
{
  PAGE_PTR page_p;
  VPID vpid;

  vpid.volid = oid_p->volid;
  vpid.pageid = oid_p->pageid;

  page_p = pgbuf_fix (thread_p, &vpid, OLD_PAGE, latch,
		      PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT,
		  3, oid_p->volid, oid_p->pageid, oid_p->slotid);
	}
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  if (spage_get_record (page_p, oid_p->slotid, record_p, is_peek) !=
      S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      return NULL;
    }

  *out_repr_count_p = CEIL_PTVDIV (record_p->length, CATALOG_REPR_ITEM_SIZE);
  return page_p;
}

static PAGE_PTR
catalog_get_representation_record_after_search (THREAD_ENTRY * thread_p,
						OID * class_id_p,
						RECDES * record_p, int latch,
						int is_peek, OID * oid_p,
						int *out_repr_count_p)
{
  EH_SEARCH search;

  search =
    ehash_search (thread_p, &catalog_Id.xhid, (void *) class_id_p, oid_p);
  if (search != EH_KEY_FOUND)
    {
      if (search == EH_KEY_NOTFOUND)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_UNKNOWN_CLASSID, 3,
		  class_id_p->volid, class_id_p->pageid, class_id_p->slotid);
	}
      return NULL;
    }

  return catalog_get_representation_record (thread_p, oid_p, record_p, latch,
					    is_peek, out_repr_count_p);
}

static int
catalog_adjust_directory_count (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				RECDES * record_p, int delta)
{
  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  spage_get_record (page_p, CATALOG_HEADER_SLOT, record_p, COPY);

  log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p,
			   CATALOG_HEADER_SLOT, record_p);

  CATALOG_PUT_PGHEADER_DIR_COUNT
    (record_p->data, CATALOG_GET_PGHEADER_DIR_COUNT (record_p->data) + delta);

  if (spage_update (thread_p, page_p, CATALOG_HEADER_SLOT, record_p) !=
      SP_SUCCESS)
    {
      return ER_FAILED;
    }

  log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p,
			   CATALOG_HEADER_SLOT, record_p);

  return NO_ERROR;
}

static void
catalog_delete_key (OID * class_id_p, REPR_ID repr_id)
{
  CATALOG_KEY catalog_key;
  int rv;

  catalog_key.page_id = class_id_p->pageid;
  catalog_key.volid = class_id_p->volid;
  catalog_key.slot_id = class_id_p->slotid;
  catalog_key.repr_id = repr_id;

  rv = pthread_mutex_lock (&catalog_Hash_table_lock);
  mht_rem (catalog_Hash_table, (void *) &catalog_key, NULL, NULL);
  pthread_mutex_unlock (&catalog_Hash_table_lock);
}

static char *
catalog_find_representation_item_position (INT16 repr_id, int repr_cnt,
					   char *repr_p, int *out_position_p)
{
  int position = 0;

  while (position < repr_cnt
	 && repr_id != CATALOG_GET_REPR_ITEM_REPRID (repr_p))
    {
      position++;
      repr_p += CATALOG_REPR_ITEM_SIZE;
    }

  *out_position_p = position;
  return repr_p;
}

static int
catalog_insert_representation_item (THREAD_ENTRY * thread_p,
				    RECDES * record_p, void *key)
{
  PAGE_PTR page_p;
  VPID page_id;
  PGSLOTID slot_id;
  PGLENGTH new_space;
  OID oid;

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

  log_append_undoredo_recdes2 (thread_p, RVCT_INSERT, &catalog_Id.vfid,
			       page_p, slot_id, NULL, record_p);

  if (catalog_adjust_directory_count (thread_p, page_p, record_p, 1) !=
      NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      return ER_FAILED;
    }

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  pgbuf_set_dirty (thread_p, page_p, FREE);
  catalog_update_max_space (&page_id, new_space);

  oid.volid = page_id.volid;
  oid.pageid = page_id.pageid;
  oid.slotid = slot_id;

  if (ehash_insert (thread_p, &catalog_Id.xhid, key, &oid) == NULL)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}


/*
 * catalog_put_representation_item () -
 *   return: NO_ERROR or ER_FAILED
 *   class_id(in): Class object identifier
 *   repr_item(in): Representation Item
 *
 * Note: The given representation item is inserted to the class
 * directory, if any. If there is no class directory, one is
 * created to contain the given item and catalog index is
 * inserted an entry for the class to point to the class
 * directory. If there is already a class directory, the given
 * item replaces the old representation item with the same
 * representation identifier, if any, otherwise it is added to
 * the directory. If enlarged directory can not be put back to
 * its orginal page, it is moved to another page and catalog
 * index is updated to point to the class directory.
 */
static int
catalog_put_representation_item (THREAD_ENTRY * thread_p, OID * class_id_p,
				 CATALOG_REPR_ITEM * repr_item_p)
{
  EH_SEARCH search;
  PAGE_PTR page_p;
  VPID page_id;
  PGSLOTID slot_id;
  PGLENGTH new_space;
  void *key;
  OID oid;
  char page_header_data[CATALOG_PAGE_HEADER_SIZE + MAX_ALIGNMENT];
  char *aligned_page_header_data;
  RECDES record = { 0, -1, REC_HOME, NULL };
  RECDES tmp_record = { 0, -1, REC_HOME, NULL };
  int repr_pos, repr_count;
  char *repr_p;
  int success;
  char *old_rec_data;

  aligned_page_header_data = PTR_ALIGN (page_header_data, MAX_ALIGNMENT);

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  key = (void *) class_id_p;

  search = ehash_search (thread_p, &catalog_Id.xhid, key, &oid);
  if (search != EH_KEY_FOUND)
    {
      if (search == EH_ERROR_OCCURRED)
	{
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}

      record.length = CATALOG_REPR_ITEM_SIZE;
      catalog_put_repr_item_to_record (record.data, repr_item_p);

      if (catalog_insert_representation_item (thread_p, &record, key) !=
	  NO_ERROR)
	{
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}
    }
  else
    {
      /* get old directory for the class */
      page_id.volid = oid.volid;
      page_id.pageid = oid.pageid;

      page_p = catalog_get_representation_record (thread_p, &oid, &record,
						  PGBUF_LATCH_WRITE, COPY,
						  &repr_count);
      if (page_p == NULL)
	{
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}

      repr_p =
	catalog_find_representation_item_position (repr_item_p->repr_id,
						   repr_count, record.data,
						   &repr_pos);

      if (repr_pos < repr_count)
	{
	  log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid,
				   page_p, oid.slotid, &record);

	  page_id.pageid = CATALOG_GET_REPR_ITEM_PAGEID_PAGEID (repr_p);
	  page_id.volid = CATALOG_GET_REPR_ITEM_PAGEID_VOLID (repr_p);
	  slot_id = CATALOG_GET_REPR_ITEM_SLOTID (repr_p);

	  catalog_delete_key (class_id_p, repr_item_p->repr_id);
	  catalog_put_repr_item_to_record (repr_p, repr_item_p);

	  if (spage_update (thread_p, page_p, oid.slotid, &record) !=
	      SP_SUCCESS)
	    {
	      recdes_free_data_area (&record);
	      pgbuf_unfix_and_init (thread_p, page_p);
	      return ER_FAILED;
	    }

	  log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid,
				   page_p, oid.slotid, &record);
	  pgbuf_set_dirty (thread_p, page_p, FREE);

	  /* delete the old representation */
	  if (catalog_drop_disk_representation_from_page
	      (thread_p, &page_id, slot_id) != NO_ERROR)
	    {
	      recdes_free_data_area (&record);
	      return ER_FAILED;
	    }
	}
      else			/* a new representation identifier */
	{
	  /* copy old directory for logging purposes */

	  if (recdes_allocate_data_area (&tmp_record, record.length) !=
	      NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, page_p);
	      recdes_free_data_area (&record);
	      return ER_FAILED;
	    }

	  tmp_record.length = record.length;
	  tmp_record.type = record.type;
	  memcpy (tmp_record.data, record.data, record.length);

	  /* extend the directory */

	  if (repr_count + 1 >= CATALOG_MAX_REPR_COUNT)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_CT_REPRCNT_OVERFLOW, 1, CATALOG_MAX_REPR_COUNT);
	    }
	  else
	    {
	      /* add the new representation item */
	      old_rec_data = record.data;
	      record.data += record.length;
	      catalog_put_repr_item_to_record (record.data, repr_item_p);
	      record.length += CATALOG_REPR_ITEM_SIZE;
	      record.data = old_rec_data;
	      record.area_size = DB_PAGESIZE;
	    }

	  log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid,
				   page_p, oid.slotid, &tmp_record);

	  success = spage_update (thread_p, page_p, oid.slotid, &record);
	  if (success == SP_SUCCESS)
	    {
	      recdes_free_data_area (&tmp_record);
	      log_append_redo_recdes2 (thread_p, RVCT_UPDATE,
				       &catalog_Id.vfid, page_p, oid.slotid,
				       &record);
	      pgbuf_set_dirty (thread_p, page_p, FREE);
	    }
	  else if (success == SP_DOESNT_FIT)
	    {
	      /* the directory needs to be deleted from the current page and
	         moved to another page. */

	      ehash_delete (thread_p, &catalog_Id.xhid, key);
	      log_append_undoredo_recdes2 (thread_p, RVCT_DELETE,
					   &catalog_Id.vfid, page_p,
					   oid.slotid, &tmp_record, NULL);
	      recdes_free_data_area (&tmp_record);

	      spage_delete (thread_p, page_p, oid.slotid);
	      new_space = spage_max_space_for_new_record (thread_p, page_p);

	      recdes_set_data_area (&tmp_record, aligned_page_header_data,
				    CATALOG_PAGE_HEADER_SIZE);

	      if (catalog_adjust_directory_count (thread_p, page_p,
						  &tmp_record,
						  -1) != NO_ERROR)
		{
		  pgbuf_unfix_and_init (thread_p, page_p);
		  recdes_free_data_area (&record);
		  return ER_FAILED;
		}

	      pgbuf_set_dirty (thread_p, page_p, FREE);
	      catalog_update_max_space (&page_id, new_space);

	      if (catalog_insert_representation_item (thread_p, &record,
						      key) != NO_ERROR)
		{
		  recdes_free_data_area (&record);
		  return ER_FAILED;
		}
	    }
	  else
	    {
	      recdes_free_data_area (&record);
	      recdes_free_data_area (&tmp_record);
	      pgbuf_unfix_and_init (thread_p, page_p);
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
catalog_get_representation_item (THREAD_ENTRY * thread_p, OID * class_id_p,
				 CATALOG_REPR_ITEM * repr_item_p)
{
  PAGE_PTR page_p;
  RECDES record;
  OID oid;
  CATALOG_VALUE *catalog_value_p;
  int repr_pos, repr_count;
  char *repr_p;
  CATALOG_KEY catalog_key;
  int rv;

  catalog_key.page_id = class_id_p->pageid;
  catalog_key.volid = class_id_p->volid;
  catalog_key.slot_id = class_id_p->slotid;
  catalog_key.repr_id = repr_item_p->repr_id;

  rv = pthread_mutex_lock (&catalog_Hash_table_lock);
  catalog_value_p =
    (CATALOG_VALUE *) mht_get (catalog_Hash_table, (void *) &catalog_key);

  if (catalog_value_p != NULL)
    {
      repr_item_p->page_id.volid = catalog_value_p->page_id.volid;
      repr_item_p->page_id.pageid = catalog_value_p->page_id.pageid;
      repr_item_p->slot_id = catalog_value_p->slot_id;
      pthread_mutex_unlock (&catalog_Hash_table_lock);
    }
  else
    {
      pthread_mutex_unlock (&catalog_Hash_table_lock);

      page_p =
	catalog_get_representation_record_after_search (thread_p, class_id_p,
							&record,
							PGBUF_LATCH_READ,
							PEEK, &oid,
							&repr_count);
      if (page_p == NULL)
	{
	  return ER_FAILED;
	}

      repr_p =
	catalog_find_representation_item_position (repr_item_p->repr_id,
						   repr_count, record.data,
						   &repr_pos);

      if (repr_pos == repr_count)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_UNKNOWN_REPRID, 1,
		  repr_item_p->repr_id);
	  return ER_FAILED;
	}

      repr_item_p->page_id.pageid =
	CATALOG_GET_REPR_ITEM_PAGEID_PAGEID (repr_p);
      repr_item_p->page_id.volid =
	CATALOG_GET_REPR_ITEM_PAGEID_VOLID (repr_p);
      repr_item_p->slot_id = CATALOG_GET_REPR_ITEM_SLOTID (repr_p);
      pgbuf_unfix_and_init (thread_p, page_p);

      rv = pthread_mutex_lock (&catalog_Hash_table_lock);

      if (catalog_key_value_entry_point >= (CATALOG_KEY_VALUE_ARRAY_SIZE - 1)
	  || mht_count (catalog_Hash_table) > CATALOG_HASH_SIZE)
	{
	  /* hash table full */
	  (void) mht_clear (catalog_Hash_table, NULL, NULL);
	  catalog_key_value_entry_point = 0;
	}

      catalog_Keys[catalog_key_value_entry_point] = catalog_key;
      catalog_Values[catalog_key_value_entry_point].page_id.pageid =
	repr_item_p->page_id.pageid;
      catalog_Values[catalog_key_value_entry_point].page_id.volid =
	repr_item_p->page_id.volid;
      catalog_Values[catalog_key_value_entry_point].slot_id =
	repr_item_p->slot_id;

      if (mht_put (catalog_Hash_table,
		   (void *) &catalog_Keys[catalog_key_value_entry_point],
		   (void *) &catalog_Values[catalog_key_value_entry_point]) ==
	  NULL)
	{
#if defined(CT_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"ct_get_repritem: Insertion to hash table"
			"failed.\n Key: Class_Id: { %d , %d , %d } Repr: %d",
			class_id_p->pageid, class_id_p->volid,
			class_id_p->slotid, repr_item_p->repr_id);
#endif /* CT_DEBUG */
	}
      else
	{
	  catalog_key_value_entry_point++;
	}
      pthread_mutex_unlock (&catalog_Hash_table_lock);
    }

  return NO_ERROR;
}

static int
catalog_drop_directory (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			RECDES * record_p, OID * oid_p, OID * class_id_p)
{
  log_append_undoredo_recdes2 (thread_p, RVCT_DELETE, &catalog_Id.vfid,
			       page_p, oid_p->slotid, record_p, NULL);

  if (spage_delete (thread_p, page_p, oid_p->slotid) != oid_p->slotid)
    {
      return ER_FAILED;
    }

  if (catalog_adjust_directory_count (thread_p, page_p, record_p, -1) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  if (ehash_delete (thread_p, &catalog_Id.xhid, (void *) class_id_p) == NULL)
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
catalog_drop_representation_item (THREAD_ENTRY * thread_p, OID * class_id_p,
				  CATALOG_REPR_ITEM * repr_item_p)
{
  PAGE_PTR page_p;
  PGLENGTH new_space;
  char *repr_p, *next_p;
  int repr_pos, repr_count;
  RECDES record;
  OID oid;
  VPID vpid;
  CATALOG_REPR_ITEM tmp_repr_item;

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  page_p =
    catalog_get_representation_record_after_search (thread_p, class_id_p,
						    &record,
						    PGBUF_LATCH_WRITE, COPY,
						    &oid, &repr_count);
  if (page_p == NULL)
    {
      recdes_free_data_area (&record);
      return ER_FAILED;
    }

  repr_p =
    catalog_find_representation_item_position (repr_item_p->repr_id,
					       repr_count, record.data,
					       &repr_pos);

  if (repr_pos == repr_count)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      recdes_free_data_area (&record);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_UNKNOWN_REPRID, 1,
	      repr_item_p->repr_id);
      return ER_FAILED;
    }

  repr_item_p->page_id.pageid = CATALOG_GET_REPR_ITEM_PAGEID_PAGEID (repr_p);
  repr_item_p->page_id.volid = CATALOG_GET_REPR_ITEM_PAGEID_VOLID (repr_p);
  repr_item_p->slot_id = CATALOG_GET_REPR_ITEM_SLOTID (repr_p);

  catalog_delete_key (class_id_p, repr_item_p->repr_id);

  if (repr_count > 1)
    {
      /* the directory will be updated */

      log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid,
			       page_p, oid.slotid, &record);

      next_p = repr_p;
      for (next_p += CATALOG_REPR_ITEM_SIZE; repr_pos < (repr_count - 1);
	   repr_pos++, repr_p += CATALOG_REPR_ITEM_SIZE, next_p +=
	   CATALOG_REPR_ITEM_SIZE)
	{
	  catalog_get_repr_item_from_record (&tmp_repr_item, next_p);
	  catalog_put_repr_item_to_record (repr_p, &tmp_repr_item);
	}

      record.length -= CATALOG_REPR_ITEM_SIZE;

      if (spage_update (thread_p, page_p, oid.slotid, &record) != SP_SUCCESS)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}

      log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid,
			       page_p, oid.slotid, &record);
    }
  else
    {
      if (catalog_drop_directory (thread_p, page_p, &record, &oid, class_id_p)
	  != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}
    }

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  pgbuf_set_dirty (thread_p, page_p, FREE);

  vpid.volid = oid.volid;
  vpid.pageid = oid.pageid;
  catalog_update_max_space (&vpid, new_space);

  recdes_free_data_area (&record);
  return NO_ERROR;
}

static void
catalog_copy_btree_statistic (BTREE_STATS * new_btree_stats_p,
			      int new_btree_stats_count,
			      BTREE_STATS * pre_btree_stats_p,
			      int pre_btree_stats_count)
{
  BTREE_STATS *pre_stats_p, *new_stats_p;
  int i, j, k;

  for (i = 0, new_stats_p = new_btree_stats_p;
       i < new_btree_stats_count; i++, new_stats_p++)
    {
      for (j = 0, pre_stats_p = pre_btree_stats_p;
	   j < pre_btree_stats_count; j++, pre_stats_p++)
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
catalog_copy_disk_attributes (DISK_ATTR * new_attrs_p, int new_attr_count,
			      DISK_ATTR * pre_attrs_p, int pre_attr_count)
{
  DISK_ATTR *pre_attr_p, *new_attr_p;
  int i, j;

  for (i = 0, new_attr_p = new_attrs_p; i < new_attr_count; i++, new_attr_p++)
    {
      for (j = 0, pre_attr_p = pre_attrs_p; j < pre_attr_count;
	   j++, pre_attr_p++)
	{
	  if (new_attr_p->id != pre_attr_p->id)
	    {
	      continue;
	    }

	  catalog_copy_btree_statistic (new_attr_p->bt_stats,
					new_attr_p->n_btstats,
					pre_attr_p->bt_stats,
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
  if (catalog_Hash_table != NULL)
    {
      catalog_finalize ();
    }

  VFID_COPY (&catalog_Id.xhid, &catalog_id_p->xhid);
  catalog_Id.xhid.pageid = catalog_id_p->xhid.pageid;
  catalog_Id.vfid.fileid = catalog_id_p->vfid.fileid;
  catalog_Id.vfid.volid = catalog_id_p->vfid.volid;
  catalog_Id.hpgid = catalog_id_p->hpgid;

  catalog_Hash_table = mht_create ("Cat_Hash_Table", CATALOG_HASH_SIZE,
				   catalog_hash, catalog_compare);
  catalog_key_value_entry_point = 0;

  catalog_Max_record_size =
    spage_max_record_size () - CATALOG_PAGE_HEADER_SIZE -
    CATALOG_MAX_SLOT_ID_SIZE - CATALOG_MAX_SLOT_ID_SIZE;

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
  if (catalog_Hash_table != NULL)
    {
      (void) mht_destroy (catalog_Hash_table);
      catalog_Hash_table = NULL;
    }
}

/*
 * catalog_create () -
 *   return: CTID * (catid on success and NULL on failure)
 *   catalog_id_p(out): Catalog identifier.
 *               All the fields in the identifier are set except the catalog
 *               and catalog index volume identifiers which should have been
 *               set by the caller.
 *   expected_pages(in): Expected number of pages in the catalog
 *   expected_index_entries(in): Expected number of entries in the catalog index
 *
 * Note: Creates the catalog and an index that will be used for fast
 * catalog search. The index used is an extendible hashing index.
 * The first page (header page) of the catalog is allocated and
 * catalog header information is initialized.
 */
CTID *
catalog_create (THREAD_ENTRY * thread_p, CTID * catalog_id_p,
		DKNPAGES expected_pages, DKNPAGES expected_index_entries)
{
  PAGE_PTR page_p;
  VPID vpid;
  int new_space;
  bool is_overflow_page = false;

  if (xehash_create (thread_p, &catalog_id_p->xhid, DB_TYPE_OBJECT,
		     expected_index_entries, oid_Root_class_oid, -1,
		     false) == NULL)
    {
      return NULL;
    }

  if (file_create (thread_p, &catalog_id_p->vfid, expected_pages,
		   FILE_CATALOG, NULL, &vpid, 1) == NULL)
    {
      (void) xehash_destroy (thread_p, &catalog_id_p->xhid);
      return NULL;
    }

  if (catalog_initialize_new_page (thread_p, &catalog_id_p->vfid,
				   FILE_CATALOG, &vpid, 1,
				   (void *) is_overflow_page) == false)
    {
      (void) xehash_destroy (thread_p, &catalog_id_p->xhid);
      (void) file_destroy (thread_p, &catalog_id_p->vfid);
      return NULL;
    }

  catalog_id_p->hpgid = vpid.pageid;

  /*
   * Note: we fetch the page as old since it was initialized during the
   * allocation by catalog_initialize_new_page, we want the current
   * contents of the page.
   */

  page_p = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		      PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      (void) xehash_destroy (thread_p, &catalog_id_p->xhid);
      (void) file_destroy (thread_p, &catalog_id_p->vfid);
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  if (catalog_is_header_initialized == false)
    {
      catalog_initialize_max_space (&catalog_Max_space);
      catalog_is_header_initialized = true;
    }

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  catalog_update_max_space (&vpid, new_space);
  pgbuf_unfix_and_init (thread_p, page_p);

  return catalog_id_p;
}

/*TODO: check not use */
#if 0
/*
 * catalog_destroy () -
 *   return: NO_ERROR or ER_FAILED
 *
 * Note: Destroys the catalog and its associated index. After the
 * routine is called, the catalog volume identifier and catalog
 * index identifier are not valid any more.
 */
int
catalog_destroy (void)
{
  /* destroy catalog index and catalog */
  if (xehash_destroy (&catalog_Id.xhid) != NO_ERROR
      || file_destroy (&catalog_Id.vfid) != NO_ERROR)
    {
      return ER_FAILED;
    }

  mht_destroy (catalog_Hash_table);

  return NO_ERROR;
}
#endif

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
  PAGE_PTR page_p;
  VPID vpid;
  int page_count;
  int i;

  /* reinitialize catalog hinted page information, this is needed since
   * the page pointed by this header may be deallocated by this routine
   * and the hinted page structure may have a dangling page pointer.
   */
  catalog_initialize_max_space (&catalog_Max_space);

  page_count = file_get_numpages (thread_p, &catalog_Id.vfid);
  if (page_count > 0)
    {
      /* fetch all the catalog pages and deallocate empty ones */
      for (i = 0; i < page_count; i++)
	{
	  if (file_find_nthpages (thread_p, &catalog_Id.vfid, &vpid, i, 1) <=
	      0)
	    {
	      return ER_FAILED;
	    }

	  page_p = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
	  if (page_p == NULL)
	    {
	      return ER_FAILED;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

	  /* page is empty?  */
	  if (spage_number_of_records (page_p) <= 1)
	    {
	      /* page is empty: has only header record; so deallocate it */
	      pgbuf_unfix_and_init (thread_p, page_p);

	      if (file_dealloc_page (thread_p, &catalog_Id.vfid, &vpid) !=
		  NO_ERROR)
		{
		  return ER_FAILED;
		}

	      /* Decrement the deallocated page... to continue */
	      i--;
	      page_count--;
	    }
	  else
	    {
	      pgbuf_unfix_and_init (thread_p, page_p);
	    }
	}
    }

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
 *   class_id(in): Class identifier
 *   repr_id(in): Disk Representation identifier
 *   disk_reprp(in): Pointer to the disk representation structure
 *
 */
int
catalog_add_representation (THREAD_ENTRY * thread_p, OID * class_id_p,
			    REPR_ID repr_id, DISK_REPR * disk_repr_p)
{
  CATALOG_REPR_ITEM repr_item;
  CATALOG_RECORD catalog_record;
  PAGE_PTR page_p;
  PGLENGTH new_space;
  VPID vpid;
  DISK_ATTR *disk_attr_p;
  BTREE_STATS *btree_stats_p;
  int size, i, j;
  char *data;
  PGSLOTID remembered_slot_id = NULL_SLOTID;

  if (OID_ISTEMP (class_id_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_CLASSID, 3,
	      class_id_p->volid, class_id_p->pageid, class_id_p->slotid);
      return ER_CT_INVALID_CLASSID;
    }

  if (repr_id == NULL_REPRID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_REPRID, 1,
	      repr_id);
      return ER_CT_INVALID_REPRID;
    }

  size = CATALOG_DISK_REPR_SIZE;
  size += catalog_sum_disk_attribute_size (disk_repr_p->fixed,
					   disk_repr_p->n_fixed);
  size += catalog_sum_disk_attribute_size (disk_repr_p->variable,
					   disk_repr_p->n_variable);

  page_p = catalog_find_optimal_page (thread_p, size, &vpid);
  if (page_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  repr_item.page_id.pageid = vpid.pageid;
  repr_item.page_id.volid = vpid.volid;
  repr_item.slot_id = NULL_SLOTID;
  repr_item.repr_id = repr_id;

  data = (char *) db_private_alloc (thread_p, DB_PAGESIZE);
  if (!data)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  catalog_record.vpid.pageid = repr_item.page_id.pageid;
  catalog_record.vpid.volid = repr_item.page_id.volid;
  catalog_record.slotid = NULL_SLOTID;
  catalog_record.page_p = page_p;
  catalog_record.recdes.area_size =
    spage_max_space_for_new_record (thread_p,
				    page_p) - CATALOG_MAX_SLOT_ID_SIZE;
  catalog_record.recdes.length = 0;
  catalog_record.recdes.type = REC_HOME;
  catalog_record.recdes.data = data;
  catalog_record.offset = 0;

  if (catalog_store_disk_representation (thread_p, disk_repr_p,
					 &catalog_record,
					 &remembered_slot_id) != NO_ERROR)
    {
      db_private_free_and_init (thread_p, data);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  new_space = spage_max_space_for_new_record (thread_p,
					      catalog_record.page_p);
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

      if (catalog_store_disk_attribute (thread_p, disk_attr_p,
					&catalog_record,
					&remembered_slot_id) != NO_ERROR)
	{
	  db_private_free_and_init (thread_p, data);

	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (catalog_store_attribute_value (thread_p, disk_attr_p->value,
					 disk_attr_p->val_length,
					 &catalog_record,
					 &remembered_slot_id) != NO_ERROR)
	{
	  db_private_free_and_init (thread_p, data);

	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      for (j = 0; j < disk_attr_p->n_btstats; j++)
	{
	  btree_stats_p = &disk_attr_p->bt_stats[j];

	  assert (btree_stats_p->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	  if (catalog_store_btree_statistics (thread_p, btree_stats_p,
					      &catalog_record,
					      &remembered_slot_id) !=
	      NO_ERROR)
	    {
	      db_private_free_and_init (thread_p, data);

	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	}
    }

  catalog_record.recdes.length = catalog_record.offset;
  catalog_record.offset = catalog_record.recdes.area_size;

  if (catalog_put_record_into_page (thread_p, &catalog_record, 0,
				    &remembered_slot_id) != NO_ERROR)
    {
      db_private_free_and_init (thread_p, data);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  pgbuf_unfix_and_init (thread_p, catalog_record.page_p);

  repr_item.slot_id = catalog_record.slotid;
  if (catalog_put_representation_item (thread_p, class_id_p, &repr_item) !=
      NO_ERROR)
    {
      db_private_free_and_init (thread_p, data);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  db_private_free_and_init (thread_p, data);

  return NO_ERROR;
}

/*
 * catalog_add_class_info () - Add class information to the catalog
 *   return: int
 *   class_id(in): Class identifier
 *   cls_info(in): Pointer to class specific information structure
 *
 */
int
catalog_add_class_info (THREAD_ENTRY * thread_p, OID * class_id_p,
			CLS_INFO * class_info_p)
{
  PAGE_PTR page_p;
  VPID page_id;
  PGLENGTH new_space;
  RECDES record = { -1, -1, REC_HOME, NULL };
  CATALOG_REPR_ITEM repr_item = { {NULL_PAGEID, NULL_VOLID}, NULL_REPRID,
  NULL_SLOTID
  };
  int success;

  if (OID_ISTEMP (class_id_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_CLASSID, 3,
	      class_id_p->volid, class_id_p->pageid, class_id_p->slotid);
      return ER_CT_INVALID_CLASSID;
    }

  page_p =
    catalog_find_optimal_page (thread_p, CATALOG_CLS_INFO_SIZE, &page_id);
  if (page_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  repr_item.page_id = page_id;

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* copy the given representation into a slotted page record */
  record.length = CATALOG_CLS_INFO_SIZE;
  catalog_put_class_info_to_record (record.data, class_info_p);
  memset ((char *) record.data +
	  (CATALOG_CLS_INFO_SIZE - CATALOG_CLS_INFO_RESERVED), 0,
	  CATALOG_CLS_INFO_RESERVED);

  success = spage_insert (thread_p, page_p, &record, &repr_item.slot_id);
  if (success != SP_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      recdes_free_data_area (&record);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  log_append_undoredo_recdes2 (thread_p, RVCT_INSERT, &catalog_Id.vfid,
			       page_p, repr_item.slot_id, NULL, &record);

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  pgbuf_set_dirty (thread_p, page_p, FREE);

  catalog_update_max_space (&repr_item.page_id, new_space);

  if (catalog_put_representation_item (thread_p, class_id_p, &repr_item) !=
      NO_ERROR)
    {
      recdes_free_data_area (&record);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  recdes_free_data_area (&record);
  return NO_ERROR;
}

/*
 * catalog_update_class_info () - Update class information to the catalog
 *   return: CLS_INFO* or NULL
 *   class_id(in): Class identifier
 *   cls_info(in): Pointer to class specific information structure
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
catalog_update_class_info (THREAD_ENTRY * thread_p, OID * class_id_p,
			   CLS_INFO * class_info_p, bool skip_logging)
{
  PAGE_PTR page_p;
  char data[CATALOG_CLS_INFO_SIZE + MAX_ALIGNMENT], *aligned_data;
  RECDES record =
    { CATALOG_CLS_INFO_SIZE, CATALOG_CLS_INFO_SIZE, REC_HOME, NULL };
  CATALOG_REPR_ITEM repr_item = { {NULL_PAGEID, NULL_VOLID}, NULL_REPRID,
  NULL_SLOTID
  };
  LOG_DATA_ADDR addr;

  aligned_data = PTR_ALIGN (data, MAX_ALIGNMENT);

  repr_item.repr_id = NULL_REPRID;
  if (catalog_get_representation_item (thread_p, class_id_p, &repr_item) !=
      NO_ERROR)
    {
      return NULL;
    }

  page_p = pgbuf_fix (thread_p, &repr_item.page_id, OLD_PAGE,
		      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  recdes_set_data_area (&record, aligned_data, CATALOG_CLS_INFO_SIZE);
  if (spage_get_record (page_p, repr_item.slot_id, &record, COPY) !=
      S_SUCCESS)
    {
#if defined(CT_DEBUG)
      if (er_errid () == ER_SP_UNKNOWN_SLOTID)
	er_log_debug (ARG_FILE_LINE, "catalog_update_class_info: ",
		      "no class information record found in catalog.\n"
		      "possibly catalog index points to a non_existent "
		      "disk repr.\n");
#endif /* CT_DEBUG */
      pgbuf_unfix_and_init (thread_p, page_p);
      return NULL;
    }

  if (skip_logging != true)
    {
      log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid,
			       page_p, repr_item.slot_id, &record);
    }

  catalog_put_class_info_to_record (record.data, class_info_p);
  memset ((char *) record.data +
	  (CATALOG_CLS_INFO_SIZE - CATALOG_CLS_INFO_RESERVED), 0,
	  CATALOG_CLS_INFO_RESERVED);

  if (spage_update (thread_p, page_p, repr_item.slot_id, &record) !=
      SP_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      return NULL;
    }

  if (skip_logging != true)
    {
      log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid,
			       page_p, repr_item.slot_id, &record);
    }

  if (skip_logging == true)
    {
      addr.vfid = &catalog_Id.vfid;
      addr.pgptr = page_p;
      addr.offset = repr_item.slot_id;

      log_skip_logging (thread_p, &addr);
    }
  pgbuf_set_dirty (thread_p, page_p, FREE);

  return class_info_p;
}

/*
 * catalog_drop () - Drop representation/class information  from catalog
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

  repr_item.repr_id = repr_id;
  VPID_SET_NULL (&repr_item.page_id);
  repr_item.slot_id = NULL_SLOTID;

  if ((catalog_drop_representation_item (thread_p, class_id_p, &repr_item) !=
       NO_ERROR)
      || repr_item.page_id.pageid == NULL_PAGEID
      || repr_item.page_id.volid == NULL_VOLID
      || repr_item.slot_id == NULL_SLOTID)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (catalog_drop_disk_representation_from_page (thread_p,
						  &repr_item.page_id,
						  repr_item.slot_id)
      != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

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
  OID oid;
  char *repr_p;
  REPR_ID repr_id;
  int error;

  if (OID_ISTEMP (class_id_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_CLASSID, 3,
	      class_id_p->volid, class_id_p->pageid, class_id_p->slotid);
      return ER_CT_INVALID_CLASSID;
    }

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  page_p =
    catalog_get_representation_record_after_search (thread_p, class_id_p,
						    &record, PGBUF_LATCH_READ,
						    COPY, &oid, &repr_count);
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
catalog_drop_all_representation_and_class (THREAD_ENTRY * thread_p,
					   OID * class_id_p)
{
  PAGE_PTR page_p;
  PGLENGTH new_space;
  RECDES record;
  int i, repr_count;
  OID oid;
  VPID vpid;
  char *repr_p;
  REPR_ID repr_id;
  VPID page_id;
  PGSLOTID slot_id;

  if (OID_ISTEMP (class_id_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_CLASSID, 3,
	      class_id_p->volid, class_id_p->pageid, class_id_p->slotid);
      return ER_FAILED;
    }

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  page_p =
    catalog_get_representation_record_after_search (thread_p, class_id_p,
						    &record,
						    PGBUF_LATCH_WRITE, COPY,
						    &oid, &repr_count);
  if (page_p == NULL)
    {
      recdes_free_data_area (&record);
      return ER_FAILED;
    }

  repr_p = record.data;
  vpid.volid = oid.volid;
  vpid.pageid = oid.pageid;

  /* drop each representation item one by one */
  for (i = 0; i < repr_count; i++, repr_p += CATALOG_REPR_ITEM_SIZE)
    {
      repr_id = CATALOG_GET_REPR_ITEM_REPRID (repr_p);
      page_id.pageid = CATALOG_GET_REPR_ITEM_PAGEID_PAGEID (repr_p);
      page_id.volid = CATALOG_GET_REPR_ITEM_PAGEID_VOLID (repr_p);
      slot_id = CATALOG_GET_REPR_ITEM_SLOTID (repr_p);

      catalog_delete_key (class_id_p, repr_id);

      if (catalog_drop_representation_class_from_page
	  (thread_p, &vpid, &page_p, &page_id, slot_id) != NO_ERROR)
	{
	  if (page_p != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, page_p);
	    }
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}
      assert (page_p != NULL);
    }

  if (catalog_drop_directory (thread_p, page_p, &record, &oid, class_id_p) !=
      NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      recdes_free_data_area (&record);
      return ER_FAILED;
    }

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  pgbuf_set_dirty (thread_p, page_p, FREE);
  catalog_update_max_space (&vpid, new_space);
  recdes_free_data_area (&record);

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
  OID oid;
  VPID vpid;
  char *repr_p, *last_repr_p;
  REPR_ID repr_id;
  VPID page_id;
  PGSLOTID slot_id;
  CATALOG_REPR_ITEM last_repr, class_repr;
  int is_any_dropped;

  if (OID_ISTEMP (class_id_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_CLASSID, 3,
	      class_id_p->volid, class_id_p->pageid, class_id_p->slotid);
      return ER_FAILED;
    }

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  page_p =
    catalog_get_representation_record_after_search (thread_p, class_id_p,
						    &record,
						    PGBUF_LATCH_WRITE, COPY,
						    &oid, &repr_count);
  if (page_p == NULL)
    {
      recdes_free_data_area (&record);
      return ER_FAILED;
    }

  repr_p = record.data;
  vpid.volid = oid.volid;
  vpid.pageid = oid.pageid;

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

	  if (catalog_drop_representation_class_from_page
	      (thread_p, &vpid, &page_p, &page_id, slot_id) != NO_ERROR)
	    {
	      if (page_p != NULL)
		{
		  pgbuf_unfix_and_init (thread_p, page_p);
		}
	      recdes_free_data_area (&record);
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
      log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid,
			       page_p, oid.slotid, &record);

      if (class_repr.page_id.pageid != NULL_PAGEID)
	{
	  /* first class info */
	  catalog_put_repr_item_to_record (record.data, &class_repr);
	  if (last_repr.page_id.pageid != NULL_PAGEID)
	    {
	      catalog_put_repr_item_to_record (record.data +
					       CATALOG_REPR_ITEM_SIZE,
					       &last_repr);
	      record.length = CATALOG_REPR_ITEM_SIZE * 2;
	    }
	  else
	    {
	      record.length = CATALOG_REPR_ITEM_SIZE;
	    }
	}
      else if (last_repr.page_id.pageid != NULL_PAGEID)
	{
	  /* last repr item */
	  catalog_put_repr_item_to_record (record.data, &last_repr);
	  record.length = CATALOG_REPR_ITEM_SIZE;
	}

      if (spage_update (thread_p, page_p, oid.slotid, &record) != SP_SUCCESS)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  recdes_free_data_area (&record);
	  return ER_FAILED;
	}

      log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid,
			       page_p, oid.slotid, &record);
    }

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  pgbuf_set_dirty (thread_p, page_p, FREE);
  catalog_update_max_space (&vpid, new_space);

  recdes_free_data_area (&record);
  return NO_ERROR;
}

/*
 * xcatalog_is_acceptable_new_representation () -
 *   return: NO_ERROR or ER_FAILED
 *   class_id(in): Class identifier
 *   hfid(in): Heap file identifier
 *   can_accept(out): Set to the flag to indicate if catalog can accept a new
 *                    representation.
 *
 * Note: Set can_accept flag to true if catalog can accept a new
 * representation and to false otherwise.
 *
 * The routine first checks if given class representation count
 * in the catalog has reached to the limit. If not, it simply
 * sets can_accept to true. Otherwise, it goes through all the
 * heap file object instances to find the oldest representation
 * number and deletes from the catalog the representations which
 * are older than that. If any representations have been deleted,
 * the routine sets can_accept flag to true, otherwise it
 * fails sets can_accept flag to false indicating that
 * currently catalog can not accept a new representation for the class.
 */
int
xcatalog_is_acceptable_new_representation (THREAD_ENTRY * thread_p,
					   OID * class_id_p, HFID * hfid_p,
					   int *can_accept_p)
{
  RECDES record, new_dir_record;
  OID oid;
  VPID vpid;
  PAGE_PTR page_p;
  int repr_count;
  HEAP_SCANCACHE scan_cache;
  REPR_ID min_repr_id;
  OID current_oid;
  SCAN_CODE sp_scan;
  char *repr_p, *new_repr_p;
  int older_cnt, i;
  REPR_ID repr_id;
  VPID page_id;
  PGSLOTID slot_id;
  PGLENGTH new_space;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  *can_accept_p = false;
  record.area_size = -1;

  if (OID_ISTEMP (class_id_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_CLASSID, 3,
	      class_id_p->volid, class_id_p->pageid, class_id_p->slotid);
      return ER_FAILED;
    }

  page_p =
    catalog_get_representation_record_after_search (thread_p, class_id_p,
						    &record,
						    PGBUF_LATCH_WRITE, PEEK,
						    &oid, &repr_count);
  if (page_p == NULL)
    {
      return ER_FAILED;
    }

  vpid.volid = oid.volid;
  vpid.pageid = oid.pageid;

  if (repr_count < (CATALOG_MAX_REPR_COUNT - 1))
    {
      *can_accept_p = true;
      pgbuf_unfix_and_init (thread_p, page_p);
      return NO_ERROR;
    }

  if (HFID_IS_NULL (hfid_p))
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      return NO_ERROR;
    }

  if (mvcc_Enabled && class_id_p != NULL && !OID_IS_ROOTOID (class_id_p))
    {
      /* be conservative */
      mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
      if (mvcc_snapshot == NULL)
	{
	  int error = er_errid ();
	  return (error == NO_ERROR ? ER_FAILED : error);
	}
    }
  if (heap_scancache_start (thread_p, &scan_cache, hfid_p, class_id_p, true,
			    false, mvcc_snapshot) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      return ER_FAILED;
    }

  min_repr_id = -1;
  OID_SET_NULL (&current_oid);

  while (true)
    {
      sp_scan =
	heap_next (thread_p, hfid_p, class_id_p, &current_oid, &record,
		   &scan_cache, PEEK);
      if (sp_scan != S_SUCCESS)
	{
	  break;
	}

      if ((min_repr_id == -1)
	  || (int) OR_GET_REPID (record.data) < min_repr_id)
	{
	  min_repr_id = OR_GET_REPID (record.data);
	}
    }

  if (sp_scan != S_END)
    {
      heap_scancache_end (thread_p, &scan_cache);
      pgbuf_unfix_and_init (thread_p, page_p);
      return ER_FAILED;
    }
  heap_scancache_end (thread_p, &scan_cache);

  if (min_repr_id == -1)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      if (catalog_drop_all (thread_p, class_id_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      *can_accept_p = true;
      return NO_ERROR;
    }

  if (spage_get_record (page_p, oid.slotid, &record, PEEK) != S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      return ER_FAILED;
    }

  /* find the number of representations older than min_repr_id */
  repr_p = record.data;
  older_cnt = 0;
  for (i = 0; i < repr_count; i++, repr_p += CATALOG_REPR_ITEM_SIZE)
    {
      repr_id = CATALOG_GET_REPR_ITEM_REPRID (repr_p);
      if (repr_id != NULL_REPRID && repr_id < min_repr_id)
	{
	  older_cnt++;
	}
    }

  if (older_cnt == 0)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      *can_accept_p = false;
      return NO_ERROR;
    }

  if (recdes_allocate_data_area (&record, DB_PAGESIZE) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      return ER_FAILED;
    }

  /* copy directory record */

  if (spage_get_record (page_p, oid.slotid, &record, COPY) != S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      recdes_free_data_area (&record);
      return ER_FAILED;
    }
  repr_p = record.data;


  if (recdes_allocate_data_area (&new_dir_record,
				 (repr_count -
				  older_cnt) * CATALOG_REPR_ITEM_SIZE) !=
      NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      recdes_free_data_area (&record);
      return ER_FAILED;
    }

  new_repr_p = new_dir_record.data;

  /* drop all the representations which are older than min_repr_id */
  for (i = 0; i < repr_count; i++, repr_p += CATALOG_REPR_ITEM_SIZE)
    {
      repr_id = CATALOG_GET_REPR_ITEM_REPRID (repr_p);
      page_id.pageid = CATALOG_GET_REPR_ITEM_PAGEID_PAGEID (repr_p);
      page_id.volid = CATALOG_GET_REPR_ITEM_PAGEID_VOLID (repr_p);
      slot_id = CATALOG_GET_REPR_ITEM_SLOTID (repr_p);

      if (repr_id != NULL_REPRID && repr_id < min_repr_id)
	{
	  catalog_delete_key (class_id_p, repr_id);

	  if (catalog_drop_representation_class_from_page (thread_p, &vpid,
							   &page_p, &page_id,
							   slot_id)
	      != NO_ERROR)
	    {
	      if (page_p != NULL)
		{
		  pgbuf_unfix_and_init (thread_p, page_p);
		}

	      recdes_free_data_area (&record);
	      recdes_free_data_area (&new_dir_record);
	      return ER_FAILED;
	    }
	  assert (page_p != NULL);
	}
      else
	{
	  /* copy representation item to the new directory */
	  memcpy (new_repr_p, repr_p, CATALOG_REPR_ITEM_SIZE);
	  new_repr_p += CATALOG_REPR_ITEM_SIZE;
	}
    }

  /* update the class directory itself */

  log_append_undo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p,
			   oid.slotid, &record);

  new_dir_record.length = new_dir_record.area_size;

  if (spage_update (thread_p, page_p, oid.slotid, &new_dir_record) !=
      SP_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      recdes_free_data_area (&record);
      recdes_free_data_area (&new_dir_record);
      return ER_FAILED;
    }

  log_append_redo_recdes2 (thread_p, RVCT_UPDATE, &catalog_Id.vfid, page_p,
			   oid.slotid, &new_dir_record);

  new_space = spage_max_space_for_new_record (thread_p, page_p);
  pgbuf_set_dirty (thread_p, page_p, FREE);
  catalog_update_max_space (&vpid, new_space);

  recdes_free_data_area (&record);
  recdes_free_data_area (&new_dir_record);

  *can_accept_p = true;
  return NO_ERROR;
}

/*
 * catalog_fixup_missing_disk_representation () -
 *   return:
 *   class_oid(in):
 *   reprid(in):
 */
static int
catalog_fixup_missing_disk_representation (THREAD_ENTRY * thread_p,
					   OID * class_oid_p, REPR_ID repr_id)
{
  RECDES record;
  DISK_REPR *disk_repr_p;
  HEAP_SCANCACHE scan_cache;

  heap_scancache_quick_start (&scan_cache);
  if (heap_get (thread_p, class_oid_p, &record, &scan_cache, PEEK, NULL_CHN)
      == S_SUCCESS)
    {
      disk_repr_p = orc_diskrep_from_record (thread_p, &record);
      if (disk_repr_p == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (catalog_add_representation
	  (thread_p, class_oid_p, repr_id, disk_repr_p) < 0)
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

/*
 * catalog_assign_attribute () -
 *   return: NO_ERROR or ER_FAILED
 *   disk_attrp(in): pointer to DISK_ATTR structure (disk representation)
 *   catalog_record_p(in): pointer to CATALOG_RECORD structure (catalog record)
 */
static int
catalog_assign_attribute (THREAD_ENTRY * thread_p, DISK_ATTR * disk_attr_p,
			  CATALOG_RECORD * catalog_record_p)
{
  BTREE_STATS *btree_stats_p;
  int i, n_btstats;

  if (catalog_fetch_disk_attribute (thread_p, disk_attr_p,
				    catalog_record_p) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (disk_attr_p->val_length > 0)
    {
      disk_attr_p->value = db_private_alloc (thread_p,
					     disk_attr_p->val_length);
      if (disk_attr_p->value == NULL)
	{
	  return ER_FAILED;
	}
    }

  if (catalog_fetch_attribute_value (thread_p, disk_attr_p->value,
				     disk_attr_p->val_length,
				     catalog_record_p) != NO_ERROR)
    {
      return ER_FAILED;
    }

  n_btstats = disk_attr_p->n_btstats;
  if (n_btstats > 0)
    {
      disk_attr_p->bt_stats =
	(BTREE_STATS *) db_private_alloc (thread_p,
					  (sizeof (BTREE_STATS) * n_btstats));
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

	  if (catalog_fetch_btree_statistics (thread_p, btree_stats_p,
					      catalog_record_p) != NO_ERROR)
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
catalog_get_representation (THREAD_ENTRY * thread_p, OID * class_id_p,
			    REPR_ID repr_id)
{
  CATALOG_REPR_ITEM repr_item;
  CATALOG_RECORD catalog_record;
  DISK_REPR *disk_repr_p;
  DISK_ATTR *disk_attr_p;
  int i, n_attrs;
  int retry_count = 0;

start:

  disk_repr_p = NULL;

  if (repr_id == NULL_REPRID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_REPRID, 1,
	      repr_id);
      return NULL;
    }

  repr_item.page_id.pageid = NULL_PAGEID;
  repr_item.page_id.volid = NULL_VOLID;
  repr_item.slot_id = NULL_SLOTID;
  repr_item.repr_id = repr_id;
  if (catalog_get_representation_item (thread_p, class_id_p, &repr_item) !=
      NO_ERROR)
    {
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
      return NULL;
    }
  memset (disk_repr_p, 0, sizeof (DISK_REPR));

  if (catalog_fetch_disk_representation (thread_p, disk_repr_p,
					 &catalog_record) != NO_ERROR)
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
	  if (catalog_fixup_missing_disk_representation (thread_p,
							 class_id_p,
							 repr_id) == NO_ERROR
	      && retry_count++ == 0)
	    {
	      goto start;
	    }
	}
      return NULL;
    }

  n_attrs = disk_repr_p->n_fixed + disk_repr_p->n_variable;

  if (disk_repr_p->n_fixed > 0)
    {
      disk_repr_p->fixed =
	(DISK_ATTR *) db_private_alloc (thread_p, (sizeof (DISK_ATTR) *
						   disk_repr_p->n_fixed));
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
      disk_repr_p->variable =
	(DISK_ATTR *) db_private_alloc (thread_p, (sizeof (DISK_ATTR) *
						   disk_repr_p->n_variable));
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
      if (catalog_assign_attribute (thread_p, &disk_repr_p->fixed[i],
				    &catalog_record) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  for (i = 0; i < disk_repr_p->n_variable; i++)
    {
      if (catalog_assign_attribute (thread_p, &disk_repr_p->variable[i],
				    &catalog_record) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

exit_on_end:

  if (catalog_record.page_p)
    {
      pgbuf_unfix_and_init (thread_p, catalog_record.page_p);
    }

  return disk_repr_p;

exit_on_error:

  if (disk_repr_p)
    {
      catalog_free_representation (disk_repr_p);
      disk_repr_p = NULL;
    }

  goto exit_on_end;
}


static int
catalog_fixup_missing_class_info (THREAD_ENTRY * thread_p, OID * class_oid_p)
{
  RECDES record;
  HEAP_SCANCACHE scan_cache;
  CLS_INFO class_info = { {{NULL_FILEID, NULL_VOLID}, NULL_PAGEID}, 0, 0, 0 };

  heap_scancache_quick_start (&scan_cache);

  if (heap_get (thread_p, class_oid_p, &record, &scan_cache, PEEK, NULL_CHN)
      == S_SUCCESS)
    {
      orc_class_hfid_from_record (&record, &(class_info.hfid));

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

/*
 * catalog_get_class_info () - Get class information from the catalog
 *   return: Pointer to the class information structure, or NULL.
 *   class_id(in): Class identifier
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
catalog_get_class_info (THREAD_ENTRY * thread_p, OID * class_id_p)
{
  CLS_INFO *class_info_p = NULL;
  PAGE_PTR page_p;
  char data[CATALOG_CLS_INFO_SIZE + MAX_ALIGNMENT], *aligned_data;
  RECDES record =
    { CATALOG_CLS_INFO_SIZE, CATALOG_CLS_INFO_SIZE, REC_HOME, NULL };
  CATALOG_REPR_ITEM repr_item = { {NULL_PAGEID, NULL_VOLID}, NULL_REPRID,
  NULL_SLOTID
  };

  int retry = 0;

  aligned_data = PTR_ALIGN (data, MAX_ALIGNMENT);

start:

  repr_item.repr_id = NULL_REPRID;
  if (catalog_get_representation_item (thread_p, class_id_p, &repr_item) !=
      NO_ERROR)
    {
      return NULL;
    }

  page_p = pgbuf_fix (thread_p, &repr_item.page_id, OLD_PAGE,
		      PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_CATALOG);

  recdes_set_data_area (&record, aligned_data, CATALOG_CLS_INFO_SIZE);
  if (spage_get_record (page_p, repr_item.slot_id, &record, COPY) !=
      S_SUCCESS)
    {
#if defined(CT_DEBUG)
      if (er_errid () == ER_SP_UNKNOWN_SLOTID)
	{
	  /* If this case happens, means, catalog doesn't contain a page slot
	     which is referred to by index. It is possible that the slot has been
	     deleted from the catalog page, but necessary changes have not been
	     reflected to the index or to the memory hash table. */
	  er_log_debug (ARG_FILE_LINE, "catalog_get_class_info: ",
			"no class information record found in catalog.\n"
			"possibly catalog index points to a non_existent "
			"disk repr.\n");
	}
#endif /* CT_DEBUG */

      if (er_errid () == ER_SP_UNKNOWN_SLOTID)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);

	  if ((catalog_fixup_missing_class_info (thread_p, class_id_p) ==
	       NO_ERROR) && retry++ == 0)
	    {
	      goto start;
	    }
	  else
	    {
	      return NULL;
	    }
	}

      pgbuf_unfix_and_init (thread_p, page_p);
      return NULL;
    }

  class_info_p = (CLS_INFO *) db_private_alloc (thread_p, sizeof (CLS_INFO));
  if (class_info_p == NULL)
    {
      pgbuf_unfix_and_init (thread_p, page_p);
      return NULL;
    }

  catalog_get_class_info_from_record (class_info_p, record.data);
  pgbuf_unfix_and_init (thread_p, page_p);

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
catalog_get_representation_directory (THREAD_ENTRY * thread_p,
				      OID * class_id_p,
				      REPR_ID ** repr_id_set_p,
				      int *repr_count_p)
{
  OID oid;
  PAGE_PTR page_p;
  RECDES record;
  int i, item_count;
  char *repr_p;
  REPR_ID *repr_set_p;

  *repr_count_p = 0;

  page_p =
    catalog_get_representation_record_after_search (thread_p, class_id_p,
						    &record, PGBUF_LATCH_READ,
						    PEEK, &oid, &item_count);
  if (page_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  *repr_count_p = item_count;
  *repr_id_set_p = (REPR_ID *) malloc (*repr_count_p * sizeof (REPR_ID));
  if (*repr_id_set_p == NULL)
    {
      pgbuf_unfix_and_init (thread_p, page_p);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  repr_set_p = *repr_id_set_p;

  for (i = 0, repr_p = record.data; i < item_count;
       i++, repr_p += CATALOG_REPR_ITEM_SIZE)
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
catalog_get_last_representation_id (THREAD_ENTRY * thread_p,
				    OID * class_oid_p, REPR_ID * repr_id_p)
{
  OID oid;
  PAGE_PTR page_p;
  RECDES record;
  int i, item_count;
  char *repr_p;

  *repr_id_p = NULL_REPRID;

  page_p =
    catalog_get_representation_record_after_search (thread_p, class_oid_p,
						    &record, PGBUF_LATCH_READ,
						    PEEK, &oid, &item_count);
  if (page_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  for (i = 0, repr_p = record.data; i < item_count;
       i++, repr_p += CATALOG_REPR_ITEM_SIZE)
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
 *
 * Note: The disk representation information for the initial
 * representation of the class and the class specific information
 * is extracted from the record descriptor and stored in the
 * catalog. This routine must be the first routine called for the
 * storage of class representation informations in the catalog.
 */
int
catalog_insert (THREAD_ENTRY * thread_p, RECDES * record_p, OID * class_oid_p)
{
  REPR_ID new_repr_id;
  DISK_REPR *disk_repr_p = NULL;
  CLS_INFO *class_info_p = NULL;

  new_repr_id = (REPR_ID) orc_class_repid (record_p);
  if (new_repr_id == NULL_REPRID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_REPRID, 1,
	      new_repr_id);
      return ER_CT_INVALID_REPRID;
    }

  disk_repr_p = orc_diskrep_from_record (thread_p, record_p);
  if (disk_repr_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (catalog_add_representation (thread_p, class_oid_p, new_repr_id,
				  disk_repr_p) < 0)
    {
      orc_free_diskrep (disk_repr_p);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }
  orc_free_diskrep (disk_repr_p);

  class_info_p = orc_class_info_from_record (record_p);
  if (class_info_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (catalog_add_class_info (thread_p, class_oid_p, class_info_p) < 0)
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

  new_repr_id = (REPR_ID) orc_class_repid (record_p);
  if (new_repr_id == NULL_REPRID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_INVALID_REPRID, 1,
	      new_repr_id);
      return ER_CT_INVALID_REPRID;
    }

  disk_repr_p = orc_diskrep_from_record (thread_p, record_p);
  if (disk_repr_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (catalog_get_last_representation_id (thread_p, class_oid_p,
					  &current_repr_id) < 0)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (current_repr_id != NULL_REPRID)
    {
      old_repr_p = catalog_get_representation (thread_p, class_oid_p,
					       current_repr_id);
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
	  catalog_copy_disk_attributes (disk_repr_p->fixed,
					disk_repr_p->n_fixed,
					old_repr_p->fixed,
					old_repr_p->n_fixed);
	  catalog_copy_disk_attributes (disk_repr_p->variable,
					disk_repr_p->n_variable,
					old_repr_p->variable,
					old_repr_p->n_variable);

	  catalog_free_representation (old_repr_p);
	  catalog_drop (thread_p, class_oid_p, current_repr_id);
	}
    }

  if (catalog_add_representation (thread_p, class_oid_p, new_repr_id,
				  disk_repr_p) < 0)
    {
      orc_free_diskrep (disk_repr_p);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  orc_free_diskrep (disk_repr_p);

  class_info_p = catalog_get_class_info (thread_p, class_oid_p);
  if (class_info_p != NULL)
    {
      if (HFID_IS_NULL (&class_info_p->hfid))
	{
	  orc_class_hfid_from_record (record_p, &class_info_p->hfid);
	  if (!HFID_IS_NULL (&class_info_p->hfid))
	    {
	      if (catalog_update_class_info (thread_p, class_oid_p,
					     class_info_p, false) == NULL)
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
  if (catalog_drop_all_representation_and_class (thread_p, class_oid_p) !=
      NO_ERROR)
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
 *   return: DISK_VALID, DISK_VALID or DISK_ERROR
 *   class_oid(in): Class object identifier
 *
 * Note: This function checks the consistency of the class information
 * in the class and returns the consistency result. It checks
 * if the given class has an entry in the catalog index, and
 * if the catalog index entry points a valid page entry that
 * contains the representations directory for the class. It then
 * checks for each entry in the representations directory if
 * corresponding page slot entry exits.
 */
static DISK_ISVALID
catalog_check_class_consistency (THREAD_ENTRY * thread_p, OID * class_oid_p)
{
  EH_SEARCH search;
  OID oid;
  RECDES record;
  CATALOG_REPR_ITEM repr_item;
  VPID vpid;
  PAGE_PTR page_p;
  PAGE_PTR repr_page_p;
  int repr_count;
  char *repr_p;
  int i;
  DISK_ISVALID valid;

  record.area_size = -1;

  /* check if the class is known by the catalog index */
  search =
    ehash_search (thread_p, &catalog_Id.xhid, (void *) class_oid_p, &oid);
  if (search != EH_KEY_FOUND)
    {
      if (search == EH_KEY_NOTFOUND)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_UNKNOWN_CLASSID,
		  3, class_oid_p->volid, class_oid_p->pageid,
		  class_oid_p->slotid);
	  return DISK_INVALID;
	}

      return DISK_ERROR;
    }

  /* Check if the catalog index points to an existing representations
     directory. */

  vpid.volid = oid.volid;
  vpid.pageid = oid.pageid;
  valid = file_isvalid_page_partof (thread_p, &vpid, &catalog_Id.vfid);

  if (valid != DISK_VALID)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT,
		  3, oid.volid, oid.pageid, oid.slotid);
	}

      return valid;
    }

  page_p =
    catalog_get_representation_record (thread_p, &oid, &record,
				       PGBUF_LATCH_READ, PEEK, &repr_count);

  if (page_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_MISSING_REPR_DIR, 3,
	      class_oid_p->volid, class_oid_p->pageid, class_oid_p->slotid);
      return DISK_ERROR;
    }

  for (i = 0, repr_p = record.data; i < repr_count;
       i++, repr_p += CATALOG_REPR_ITEM_SIZE)
    {
      repr_item.page_id.pageid = CATALOG_GET_REPR_ITEM_PAGEID_PAGEID (repr_p);
      repr_item.page_id.volid = CATALOG_GET_REPR_ITEM_PAGEID_VOLID (repr_p);
      repr_item.slot_id = CATALOG_GET_REPR_ITEM_SLOTID (repr_p);
      repr_item.repr_id = CATALOG_GET_REPR_ITEM_REPRID (repr_p);

      valid =
	file_isvalid_page_partof (thread_p, &repr_item.page_id,
				  &catalog_Id.vfid);
      if (valid != DISK_VALID)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  return valid;
	}

      repr_page_p = pgbuf_fix (thread_p, &repr_item.page_id, OLD_PAGE,
			       PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (repr_page_p == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  return DISK_ERROR;
	}

      (void) pgbuf_check_page_ptype (thread_p, repr_page_p, PAGE_CATALOG);

      if (spage_get_record (repr_page_p, repr_item.slot_id, &record,
			    PEEK) != S_SUCCESS)
	{
	  pgbuf_unfix_and_init (thread_p, page_p);
	  pgbuf_unfix_and_init (thread_p, repr_page_p);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_MISSING_REPR_INFO,
		  4, class_oid_p->volid, class_oid_p->pageid,
		  class_oid_p->slotid, repr_item.repr_id);
	  return DISK_ERROR;
	}
      pgbuf_unfix_and_init (thread_p, repr_page_p);
    }

  pgbuf_unfix_and_init (thread_p, page_p);
  return DISK_VALID;
}

/*
 * catalog_check_class () - Check if the current class (key) is in a valid state in
 *                     the catalog.
 *   return: NO_ERROR or error code
 *   key(in): Index key pointer
 *   val(in): Index value pointer
 *   args(in): Argument data (validity status)
 */
static int
catalog_check_class (THREAD_ENTRY * thread_p, void *key, void *ignore_value,
		     void *args)
{
  DISK_ISVALID *ct_valid = (DISK_ISVALID *) args;

  *ct_valid = catalog_check_class_consistency (thread_p, (OID *) key);
  if (*ct_valid != DISK_VALID)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * catalog_check_consistency () - Check if catalog is in a consistent (valid)
 *                               state.
 *   return: DISK_VALID, DISK_VALID or DISK_ERROR
 */
DISK_ISVALID
catalog_check_consistency (THREAD_ENTRY * thread_p)
{
  DISK_ISVALID ct_valid = DISK_VALID;

  if (ehash_map (thread_p, &catalog_Id.xhid, catalog_check_class,
		 (void *) &ct_valid) == ER_FAILED && ct_valid == DISK_VALID)
    {
      return DISK_ERROR;
    }

  return ct_valid;
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
    case DB_TYPE_ELO:
      fprintf (stdout, "DB_TYPE_ELO \n");
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
  fprintf (stdout, " Source Class_Id: { %d , %d , %d } \n",
	   attr_p->classoid.volid, attr_p->classoid.pageid,
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
      fprintf (stdout, "    BTID: { %d , %d }\n",
	       bt_statsp->btid.vfid.volid, bt_statsp->btid.vfid.fileid);
      fprintf (stdout, "    Cardinality: %d (", bt_statsp->keys);

      prefix = "";
      assert (bt_statsp->pkeys_size <= BTREE_STATS_PKEYS_NUM);
      for (i = 0; i < bt_statsp->pkeys_size; i++)
	{
	  fprintf (stdout, "%s%d", prefix, bt_statsp->pkeys[i]);
	  prefix = ",";
	}

      fprintf (stdout, ") ,");
      fprintf (stdout, " Total Pages: %d , Leaf Pages: %d , Height: %d\n",
	       bt_statsp->pages, bt_statsp->leafs, bt_statsp->height);
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
      fprintf (stdout,
	       " Repr_Id : %d  N_Fixed : %d  Fixed_Length : %d "
	       " N_Variable : %d\n\n",
	       disk_repr_p->id, disk_repr_p->n_fixed,
	       disk_repr_p->fixed_length, disk_repr_p->n_variable);

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
 * ct_printind_entry () -
 *   return: NO_ERROR
 *   key(in):
 *   val(in):
 *   args(in):
 */
static int
catalog_print_entry (THREAD_ENTRY * thread_p, void *key, void *val,
		     void *ignore_args)
{
  OID *keyp, *valp;

  keyp = (OID *) key;
  valp = (OID *) val;
  (void) fprintf (stdout,
		  "\n Class_Oid: { %d , %d, %d } Val: { %d, %d, %d }\n",
		  keyp->volid, keyp->pageid, keyp->slotid, valp->volid,
		  valp->pageid, valp->slotid);

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
  CATALOG_CLASS_ID_LIST *class_id_list = NULL, *class_id_p;
  OID class_id;
  REPR_ID *repr_id_set, *repr_id_p;
  DISK_REPR *disk_repr_p = NULL;
  int repr_count;
  CLS_INFO *class_info_p = NULL;
  int i, n, overflow_count;
  VPID page_id;
  PAGE_PTR page_p;
  RECDES record;

  fprintf (fp, "\n <<<<< C A T A L O G   D U M P >>>>> \n\n");

  fprintf (fp, "\n Catalog Index Dump: \n\n");
  ehash_map (thread_p, &catalog_Id.xhid, catalog_print_entry, (void *) 0);

  fprintf (fp, "\n Catalog Dump: \n\n");
  ehash_map (thread_p, &catalog_Id.xhid, catalog_get_key_list,
	     (void *) &class_id_list);

  for (class_id_p = class_id_list;
       class_id_p->next != NULL; class_id_p = class_id_p->next)
    {
      class_id.volid = class_id_p->class_id.volid;
      class_id.pageid = class_id_p->class_id.pageid;
      class_id.slotid = class_id_p->class_id.slotid;

      fprintf (fp, " -------------------------------------------------\n");
      fprintf (fp, " CLASS_ID: { %d , %d , %d } \n", class_id.volid,
	       class_id.pageid, class_id.slotid);

      /* Get the class specific information for this class */
      class_info_p = catalog_get_class_info (thread_p, &class_id);
      if (class_info_p != NULL)
	{
	  fprintf (fp, " Class Specific Information: \n\n");
	  fprintf (fp, " HFID: { vfid = { %d , %d }, hpgid = %d }\n",
		   class_info_p->hfid.vfid.fileid,
		   class_info_p->hfid.vfid.volid, class_info_p->hfid.hpgid);
	  fprintf (fp, " Total Pages in Heap: %d\n", class_info_p->tot_pages);
	  fprintf (fp, " Total Objects: %d\n", class_info_p->tot_objects);
	  catalog_free_class_info (class_info_p);
	}


      /* get the representations identifiers set for the class */
      repr_id_set = NULL;
      catalog_get_representation_directory (thread_p, &class_id, &repr_id_set,
					    &repr_count);
      fprintf (fp, " Repr_cnt: %d \n", repr_count);

      repr_id_p = repr_id_set;
      for (repr_id_p += repr_count - 1; repr_count; repr_id_p--, repr_count--)
	{
	  if (*repr_id_p == NULL_REPRID ||
	      (disk_repr_p = catalog_get_representation (thread_p, &class_id,
							 *repr_id_p)) == NULL)
	    {
	      continue;
	    }

	  catalog_dump_representation (disk_repr_p);
	  catalog_free_representation (disk_repr_p);
	}

      if (repr_id_set != NULL)
	{
	  free_and_init (repr_id_set);
	}

      heap_classrepr_dump_all (thread_p, fp, &class_id);
    }

  catalog_free_key_list (class_id_list);
  class_id_list = NULL;

  if (dump_flag == 1)
    {
      /* slotted page dump */
      fprintf (fp, "\n Catalog Directory Dump: \n\n");
      ehash_map (thread_p, &catalog_Id.xhid, catalog_get_key_list,
		 (void *) &class_id_list);

      for (class_id_p = class_id_list; class_id_p->next != NULL;
	   class_id_p = class_id_p->next)
	{
	  class_id.volid = class_id_p->class_id.volid;
	  class_id.pageid = class_id_p->class_id.pageid;
	  class_id.slotid = class_id_p->class_id.slotid;

	  fprintf (fp,
		   " -------------------------------------------------\n");
	  fprintf (fp, " CLASS_ID: { %d , %d , %d } \n", class_id.volid,
		   class_id.pageid, class_id.slotid);

	  /* get the representations identifiers set for the class */
	  repr_id_set = NULL;
	  catalog_get_representation_directory (thread_p, &class_id,
						&repr_id_set, &repr_count);
	  fprintf (fp, " REPR_CNT: %d \n", repr_count);
	  repr_id_p = repr_id_set;

	  for (repr_id_p += repr_count - 1; repr_count;
	       repr_id_p--, repr_count--)
	    {
	      if (*repr_id_p != NULL_REPRID)
		{
		  fprintf (fp, " Repr_id: %d\n", *repr_id_p);
		}
	    }

	  if (repr_id_set != NULL)
	    {
	      free_and_init (repr_id_set);
	    }
	}

      catalog_free_key_list (class_id_list);
      class_id_list = NULL;

      n = file_get_numpages (thread_p, &catalog_Id.vfid);
      fprintf (fp, "Total Pages Count: %d\n\n", n);
      overflow_count = 0;

      /* find the list of overflow page identifiers */
      for (i = 0; i < n; i++)
	{
	  if (file_find_nthpages (thread_p, &catalog_Id.vfid, &page_id, i, 1)
	      == -1)
	    {
	      return;
	    }

	  page_p = pgbuf_fix (thread_p, &page_id, OLD_PAGE, PGBUF_LATCH_READ,
			      PGBUF_UNCONDITIONAL_LATCH);
	  if (page_p == NULL)
	    {
	      return;
	    }

	  spage_get_record (page_p, CATALOG_HEADER_SLOT, &record, PEEK);
	  if (((VPID *) record.data)->pageid != NULL_PAGEID)
	    {
	      overflow_count++;
	    }
	  pgbuf_unfix_and_init (thread_p, page_p);
	}

      fprintf (fp, "Regular Pages Count: %d\n\n", n - overflow_count);
      fprintf (fp, "Overflow Pages Count: %d\n\n", overflow_count);

      for (i = 0; i < n; i++)
	{
	  fprintf (fp, "\n-----------------------------------------------\n");
	  fprintf (fp, "\n Page %d \n", i);

	  if (file_find_nthpages (thread_p, &catalog_Id.vfid, &page_id, i, 1)
	      == -1)
	    {
	      return;
	    }

	  page_p = pgbuf_fix (thread_p, &page_id, OLD_PAGE, PGBUF_LATCH_READ,
			      PGBUF_UNCONDITIONAL_LATCH);
	  if (page_p == NULL)
	    {
	      return;
	    }

	  spage_get_record (page_p, CATALOG_HEADER_SLOT, &record, PEEK);
	  fprintf (fp, "\nPage_Id: {%d , %d}\n", page_id.pageid,
		   page_id.volid);
	  fprintf (fp, "Directory Cnt: %d\n",
		   *(int *) ((int *) record.data + 1));
	  fprintf (fp, "Overflow Page Id: {%d , %d}\n\n",
		   ((VPID *) record.data)->pageid,
		   ((VPID *) record.data)->volid);

	  spage_dump (thread_p, fp, page_p, 0);
	  pgbuf_unfix_and_init (thread_p, page_p);
	}
    }
}

static void
catalog_clear_hash_table ()
{
  int rv;

  rv = pthread_mutex_lock (&catalog_Hash_table_lock);
  if (catalog_Hash_table != NULL)
    {
      (void) mht_clear (catalog_Hash_table, NULL, NULL);
      catalog_key_value_entry_point = 0;
    }
  pthread_mutex_unlock (&catalog_Hash_table_lock);
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
  RECDES record =
    { CATALOG_PAGE_HEADER_SIZE, CATALOG_PAGE_HEADER_SIZE, REC_HOME, NULL };
  PGSLOTID slot_id;
  int success;

  aligned_data = PTR_ALIGN (data, MAX_ALIGNMENT);

  catalog_clear_hash_table ();

  (void) pgbuf_set_page_ptype (thread_p, recv_p->pgptr, PAGE_CATALOG);

  spage_initialize (thread_p, recv_p->pgptr, ANCHORED_DONT_REUSE_SLOTS,
		    MAX_ALIGNMENT, SAFEGUARD_RVSPACE);

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

  recdes_set_data_area (&record,
			(char *) (recv_p->data) + sizeof (record.type),
			recv_p->length - sizeof (record.type));
  record.length = record.area_size;
  record.type = *(INT16 *) (recv_p->data);

  success =
    spage_insert_for_recovery (thread_p, recv_p->pgptr, slot_id, &record);
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
catalog_get_cardinality (THREAD_ENTRY * thread_p, OID * class_oid,
			 DISK_REPR * rep, BTID * btid, const int key_pos,
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
  int is_global_index;
  bool is_subcls_attr_found = false;
  bool free_cls_rep = false;

  assert (class_oid != NULL && btid != NULL && cardinality != NULL);
  *cardinality = -1;

  if (rep != NULL)
    {
      disk_repr_p = rep;
    }
  else
    {
      int repr_id;
      /* get last representation id, if is not already known */
      error = catalog_get_last_representation_id (thread_p, class_oid,
						  &repr_id);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      /* get disk representation :
       * the disk representation contains a some pre-filled BTREE statistics,
       * but no partial keys info yet */
      disk_repr_p = catalog_get_representation (thread_p, class_oid, repr_id);
      if (disk_repr_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 0);
	  error = ER_UNEXPECTED;
	  goto exit;
	}
      free_disk_rep = true;
    }

  cls_rep =
    heap_classrepr_get (thread_p, class_oid, NULL, 0, &idx_cache, true);
  if (cls_rep == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 0);
      error = ER_UNEXPECTED;
      goto exit_cleanup;
    }
  free_cls_rep = true;

  /* There should be only one OR_ATTRIBUTE element that contains the index;
   * this is the element corresponding to the attribute which is the first key
   * in the index */
  p_stat_info = NULL;
  is_btree_found = false;
  /* first, search in the fixed attributes : */
  for (att_cnt = 0, disk_attr_p = disk_repr_p->fixed;
       att_cnt < disk_repr_p->n_fixed; att_cnt++, disk_attr_p++)
    {
      /* search for BTID in each BTREE_STATS element from current attribute */
      for (idx_cnt = 0, curr_stat_info = disk_attr_p->bt_stats;
	   idx_cnt < disk_attr_p->n_btstats; idx_cnt++, curr_stat_info++)
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
      for (att_cnt = 0, disk_attr_p = disk_repr_p->variable;
	   att_cnt < disk_repr_p->n_variable; att_cnt++, disk_attr_p++)
	{
	  /* search for BTID in each BTREE_STATS element from
	     current attribute */
	  for (idx_cnt = 0, curr_stat_info = disk_attr_p->bt_stats;
	       idx_cnt < disk_attr_p->n_btstats; idx_cnt++, curr_stat_info++)
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 0);
      error = ER_UNEXPECTED;
      goto exit_cleanup;
    }

  assert_release (p_stat_info != NULL);
  assert_release (BTID_IS_EQUAL (&(p_stat_info->btid), btid));
  assert_release (p_stat_info->pkeys_size > 0);
  assert_release (p_stat_info->pkeys_size <= BTREE_STATS_PKEYS_NUM);

  /* since btree_get_stats is too slow, use the old statistics.
     the user must previously execute 'update statistics on class_name',
     in order to get updated statistics. */

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
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR,
	      1, "index_cardinality()");
      goto exit_cleanup;
    }

  error = partition_get_partition_oids (thread_p, class_oid,
					&partitions, &count);
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
      error =
	partition_is_global_index (thread_p, NULL, class_oid, btid, NULL,
				   &is_global_index);
      if (error != NO_ERROR)
	{
	  goto exit_cleanup;
	}

      if (is_global_index > 0)
	{
	  /* global index, no need to search through each partition */
	  *cardinality = p_stat_info->keys;
	  goto exit_cleanup;
	}

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

	  /* load new subclass */
	  subcls_info = catalog_get_class_info (thread_p, &partitions[i]);
	  if (subcls_info == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto exit_cleanup;
	    }

	  /* get disk repr for subclass */
	  error =
	    catalog_get_last_representation_id (thread_p, &partitions[i],
						&subcls_repr_id);
	  if (error != NO_ERROR)
	    {
	      goto exit_cleanup;
	    }

	  subcls_disk_rep =
	    catalog_get_representation (thread_p, &partitions[i],
					subcls_repr_id);
	  if (subcls_disk_rep == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto exit_cleanup;
	    }

	  subcls_rep = heap_classrepr_get (thread_p, &partitions[i], NULL, 0,
					   &subcls_idx_cache, true);
	  if (subcls_rep == NULL)
	    {
	      error = ER_FAILED;
	      goto exit_cleanup;
	    }

	  is_subcls_attr_found = false;
	  for (att_cnt = 0, subcls_attr = subcls_disk_rep->fixed;
	       att_cnt < subcls_disk_rep->n_fixed; att_cnt++, subcls_attr++)
	    {
	      if (disk_attr_p->id == subcls_attr->id)
		{
		  is_subcls_attr_found = true;
		  break;
		}
	    }
	  if (!is_subcls_attr_found)
	    {
	      for (att_cnt = 0, subcls_attr = subcls_disk_rep->variable;
		   att_cnt < subcls_disk_rep->n_variable; att_cnt++,
		   subcls_attr++)
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 0);
	      error = ER_UNEXPECTED;
	      goto exit_cleanup;
	    }
	  subcls_stats =
	    stats_find_inherited_index_stats (cls_rep, subcls_rep,
					      subcls_attr, btid);
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
catalog_get_cardinality_by_name (THREAD_ENTRY * thread_p,
				 const char *class_name,
				 const char *index_name, const int key_pos,
				 int *cardinality)
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

  status = xlocator_find_class_oid (thread_p, cls_lower, &class_oid,
				    NULL_LOCK);
  if (status == LC_CLASSNAME_ERROR)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_LC_UNKNOWN_CLASSNAME, 1, cls_lower);
      return ER_FAILED;
    }

  if (status == LC_CLASSNAME_DELETED || OID_ISNULL (&class_oid))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LC_UNKNOWN_CLASSNAME,
	      1, class_name);
      goto exit;
    }

  error =
    heap_get_btid_from_index_name (thread_p, &class_oid, index_name,
				   &found_btid);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  if (BTID_IS_NULL (&found_btid))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INDEX_NOT_FOUND, 0);
      goto exit;
    }

  return catalog_get_cardinality (thread_p, &class_oid, NULL, &found_btid,
				  key_pos, cardinality);

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

  recdes_set_data_area (&record,
			(char *) (recv_p->data) + sizeof (record.type),
			recv_p->length - sizeof (record.type));
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
catalog_rv_ovf_page_logical_insert_undo (THREAD_ENTRY * thread_p,
					 LOG_RCV * recv_p)
{
  VPID *vpid_p;

  catalog_clear_hash_table ();

  vpid_p = (VPID *) recv_p->data;
  (void) file_dealloc_page (thread_p, &catalog_Id.vfid, vpid_p);

  return NO_ERROR;
}
