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
 * btree_load.c - B+-Tree Loader
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "btree_load.h"

#include "btree.h"

#include "deduplicate_key.h"
#include "double_write_buffer.hpp"
#include "external_sort.h"
#include "heap_file.h"
#include "file_io.h"
#include "file_manager.h"
#include "overflow_file.h"
#include "log_append.hpp"
#include "log_manager.h"
#include "memory_alloc.h"
#include "memory_private_allocator.hpp"
#include "mvcc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "partition.h"
#include "partition_sr.h"
#include "query_executor.h"
#include "query_opfunc.h"
#include "server_support.h"
#include "stream_to_xasl.h"
#include "thread_manager.hpp"
#include "thread_entry_task.hpp"
#include "xserver_interface.h"
#include "xasl.h"
#include "xasl_unpack_info.hpp"
#include "ftab_set.hpp"
#include "bit.h"
#ifndef NDEBUG
#include "db_value_printer.hpp"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

typedef struct btree_page BTREE_PAGE;
struct btree_page
{
  VPID vpid;
  PAGE_PTR pgptr;
  BTREE_NODE_HEADER hdr;
};

typedef int (*BT_LOAD_NEW_PAGE_FUNC) (THREAD_ENTRY *, LOAD_ARGS *, BTREE_NODE_HEADER *, int, VPID *, PAGE_PTR *);
struct load_args
{				/* This structure is never written to disk; thus logical ordering of fields is ok. */
  BTID_INT *btid;
  const char *bt_name;		/* index name */
  bool no_redo;			/* Bulk-build pages skip content redo logging only; all disk writes take the ordinary flush path (including DWB when enabled).  Durability is enforced by the pre-publication flush+sync barrier. */

  RECDES *out_recdes;		/* Pointer to current record descriptor collecting objects. */
  RECDES leaf_nleaf_recdes;	/* Record descriptor used for leaf and non-leaf records. */
  RECDES ovf_recdes;		/* Record descriptor used for overflow OID's records. */
  char *new_pos;		/* Current pointer in record being built. */
  DB_VALUE current_key;		/* Current key value */
  int max_key_size;		/* The maximum key size encountered so far; used for string types */
  int cur_key_len;		/* The length of the current key */

  /* Linked list variables */
  BTREE_NODE *push_list;
  BTREE_NODE *pop_list;

  /* Variables for managing non-leaf, leaf & overflow pages */
  BTREE_PAGE nleaf;

  BTREE_PAGE leaf;

  BTREE_PAGE ovf;

  bool overflowing;		/* Currently, are we filling in an overflow page (then, true); or a regular leaf page
				 * (then, false) */
  int n_keys;			/* Number of keys - note that in the context of MVCC, only keys that have at least one
				 * non-deleted object are counted. */

  int curr_non_del_obj_count;	/* Number of objects that have not been deleted. Unique indexes must have only one such
				 * object. */
  int curr_rec_max_obj_count;	/* Maximum number of objects for current record. */
  int curr_rec_obj_count;	/* Current number of record objects. */

  PGSLOTID last_leaf_insert_slotid;	/* Slotid of last inserted leaf record. */

  VPID vpid_first_leaf;

  MVCCID build_mvccid;
  BT_LOAD_VACUUM_ITEM *vacuum_items;
  size_t vacuum_count;
  size_t vacuum_capacity;
  size_t vacuum_payload_size;

  SORT_ARGS *sort_args;
  BT_LOAD_PROVIDER *provider;
  int worker_idx;
  BT_LOAD_NEW_PAGE_FUNC new_page_fn;
  BT_LOAD_PX_OUTCOME px_outcome;
  VPID report_first_leaf_vpid;
  VPID report_last_leaf_vpid;
  int report_local_max_key_len;
  INT64 report_n_keys;
  INT64 report_n_oids;
  INT64 report_n_nulls;
  INT64 report_overflow_key_count;
  int report_first_error;
  bool report_published;
};

typedef enum bt_load_slot_state
{
  BT_LOAD_SLOT_IDLE = 0,
  BT_LOAD_SLOT_NEED_BTREE,
  BT_LOAD_SLOT_NEED_OVF,
  BT_LOAD_SLOT_ALLOCATING,
  BT_LOAD_SLOT_READY,
  BT_LOAD_SLOT_DONE,
  BT_LOAD_SLOT_ERROR
} BT_LOAD_SLOT_STATE;

typedef struct bt_load_span BT_LOAD_SPAN;
struct bt_load_span
{
  VPID *vpids;
  int n;
  int used;
  bool owns_vpids;
  bool is_ovf;
  BT_LOAD_SPAN *next;
};

typedef enum bt_load_allocation_origin
{
  BT_LOAD_ORIGIN_POOL_INIT = 0,
  BT_LOAD_ORIGIN_REFILL,
  BT_LOAD_ORIGIN_MAIN_INLINE
} BT_LOAD_ALLOCATION_ORIGIN;

typedef struct bt_load_allocation_ledger BT_LOAD_ALLOCATION_LEDGER;
struct bt_load_allocation_ledger
{
  VFID vfid;
  VPID *vpids;
  int n;
  BT_LOAD_ALLOCATION_ORIGIN origin;
  BT_LOAD_ALLOCATION_LEDGER *next;
};

// *INDENT-OFF*
typedef struct bt_load_vpid_pool BT_LOAD_VPID_POOL;
struct bt_load_vpid_pool
{
  VPID *vpids;
  int n_published;
  std::atomic<int> cursor;
};
// *INDENT-ON*

typedef struct bt_load_worker_slot BT_LOAD_WORKER_SLOT;
struct bt_load_worker_slot
{
  BT_LOAD_SLOT_STATE state;
  int req_npages;
  BT_LOAD_SPAN *ready_span;
  BT_LOAD_SPAN *main_spans;
  BT_LOAD_SPAN *ovf_spans;
  INT64 consumed_pages;
  INT64 consumed_ovf_pages;
};

struct bt_load_provider
{
  BT_LOAD_VPID_POOL main_pool;
  BT_LOAD_VPID_POOL ovf_pool;
  VFID main_vfid;
  VFID ovf_vfid;
  VPID root_vpid;
  int est_ovf_pages;
  BT_LOAD_WORKER_SLOT *slots;
  int n_workers;
  pthread_mutex_t mtx;
  pthread_cond_t *cond_worker;
  pthread_cond_t cond_main;
  int first_error;
  BT_LOAD_SPAN *main_inline_spans;
  BT_LOAD_SPAN *main_inline_current;
  BT_LOAD_SPAN *ovf_inline_spans;
  BT_LOAD_SPAN *ovf_inline_current;
  BT_LOAD_ALLOCATION_LEDGER *ledger;
  INT64 allocated_pages;
  INT64 consumed_pages;
  INT64 consumed_ovf_pages;
  INT64 returned_pages;
};

typedef struct btree_scan_partition_info BTREE_SCAN_PART;

struct btree_scan_partition_info
{
  BTREE_SCAN bt_scan;		/* Holds information regarding the scan of the current partition. */

  BTREE_NODE_HEADER *header;	/* Header info for current partition */

  int key_cnt;			/* Number of keys in current page in the current partition. */

  PRUNING_CONTEXT pcontext;	/* Pruning context for current partition. */

  BTID btid;			/* BTID of the current partition. */
};

// *INDENT-OFF*
class index_builder_loader_context : public cubthread::entry_manager
{
  public:
    std::atomic_bool m_has_error;
    std::atomic<std::uint64_t> m_tasks_executed;
    int m_error_code;
    const TP_DOMAIN *m_key_type;
    css_conn_entry *m_conn;

    index_builder_loader_context () = default;

  protected:
    void on_create (context_type &context) override;
    void on_retire (context_type &context) override;
    void on_recycle (context_type &context) override;
};

class index_builder_loader_task: public cubthread::entry_task
{
  private:
    BTID m_btid;
    OID m_class_oid;
    int m_unique_pk;
    index_builder_loader_context &m_load_context; // Loader context.
    btree_insert_list m_insert_list;
    size_t m_memsize;

    std::atomic<int> &m_num_keys;
    std::atomic<int> &m_num_oids;
    std::atomic<int> &m_num_nulls;

  public:
    enum batch_key_status
    {
      BATCH_EMPTY,
      BATCH_CONTINUE,
      BATCH_FULL
    };

    index_builder_loader_task () = delete;

    index_builder_loader_task (const BTID *btid, const OID *class_oid, int unique_pk,
                               index_builder_loader_context &load_context, std::atomic<int> &num_keys,
			       std::atomic<int> &num_oids, std::atomic<int> &num_nulls);
    ~index_builder_loader_task ();

    // add key to key set and return true if task is ready for execution, false otherwise
    batch_key_status add_key (const DB_VALUE *key, const OID &oid);
    bool has_keys () const;

    void execute (cubthread::entry &thread_ref);

  private:
    void clear_keys ();
};

// *INDENT-ON*


static int btree_save_last_leafrec (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args);
static PAGE_PTR btree_connect_page (THREAD_ENTRY * thread_p, DB_VALUE * key, int max_key_len, VPID * pageid,
				    LOAD_ARGS * load_args, int node_level);
static int btree_build_nleafs (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, int n_nulls, int n_oids, int n_keys);

static int btree_log_page (THREAD_ENTRY * thread_p, VFID * vfid, PAGE_PTR page_ptr, bool no_redo,
			   bool flush_after_unfix);
static int btree_load_new_page (THREAD_ENTRY * thread_p, const BTID * btid, BTREE_NODE_HEADER * header, int node_level,
				VPID * vpid_new, PAGE_PTR * page_new);
static int bt_load_new_page_serial (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, BTREE_NODE_HEADER * header,
				    int node_level, VPID * vpid_new, PAGE_PTR * page_new);
static int bt_load_new_page_from_provider (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args,
					   BTREE_NODE_HEADER * header, int node_level, VPID * vpid_new,
					   PAGE_PTR * page_new);
static int bt_load_new_page_main_inline (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args,
					 BTREE_NODE_HEADER * header, int node_level, VPID * vpid_new,
					 PAGE_PTR * page_new);
static PAGE_PTR btree_proceed_leaf (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args);
static int btree_first_oid (THREAD_ENTRY * thread_p, DB_VALUE * this_key, OID * class_oid, OID * first_oid,
			    MVCC_REC_HEADER * p_mvcc_rec_header, LOAD_ARGS * load_args);
static int btree_construct_leafs (THREAD_ENTRY * thread_p, const RECDES * in_recdes, void *arg);
static int bt_load_write_record (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, void *node_rec, DB_VALUE * key,
				 BTREE_NODE_TYPE node_type, int key_type, int key_len, OID * class_oid, OID * oid,
				 BTREE_MVCC_INFO * mvcc_info, RECDES * rec);
static int btree_get_value_from_leaf_slot (THREAD_ENTRY * thread_p, BTID_INT * btid_int, PAGE_PTR leaf_ptr,
					   int slot_id, DB_VALUE * key, bool * clear_key);
#if defined(CUBRID_DEBUG)
static int btree_dump_sort_output (const RECDES * recdes, LOAD_ARGS * load_args);
#endif /* defined(CUBRID_DEBUG) */
static int btree_index_sort (THREAD_ENTRY * thread_p, SORT_ARGS * sort_args, SORT_PUT_FUNC * out_func, void *out_args);
static SORT_STATUS btree_sort_get_next (THREAD_ENTRY * thread_p, RECDES * temp_recdes, void *arg);
static int compare_driver (const void *first, const void *second, void *arg);
static int list_add (BTREE_NODE ** list, VPID * pageid);
static void list_remove_first (BTREE_NODE ** list);
static void list_clear (BTREE_NODE * list);
static int list_length (const BTREE_NODE * this_list);
#if defined(CUBRID_DEBUG)
static void list_print (const BTREE_NODE * this_list);
#endif /* defined(CUBRID_DEBUG) */
static int btree_pack_root_header (RECDES * Rec, BTREE_ROOT_HEADER * header, TP_DOMAIN * key_type);
static void btree_rv_save_root_head (long long null_delta, long long oid_delta, long long key_delta, RECDES * recdes);
static int btree_advance_to_next_slot_and_fix_page (THREAD_ENTRY * thread_p, BTID_INT * btid, VPID * vpid,
						    PAGE_PTR * pg_ptr, INT16 * slot_id, DB_VALUE * key,
						    bool * clear_key, bool is_desc, int *key_cnt,
						    BTREE_NODE_HEADER ** header, MVCC_SNAPSHOT * mvcc);
static int btree_load_check_fk (THREAD_ENTRY * thread_p, const LOAD_ARGS * load_args_local,
				const SORT_ARGS * sort_args_local);
static int btree_is_slot_visible (THREAD_ENTRY * thread_p, BTID_INT * btid, PAGE_PTR pg_ptr,
				  MVCC_SNAPSHOT * mvcc_snapshot, int slot_id, bool * is_slot_visible);

static int online_index_builder (THREAD_ENTRY * thread_p, BTID_INT * btid_int, HFID * hfids, OID * class_oids,
				 int n_classes, int *attrids, int n_attrs, FUNCTION_INDEX_INFO func_idx_info,
				 PRED_EXPR_WITH_CONTEXT * filter_pred, int *attrs_prefix_length,
				 HEAP_CACHE_ATTRINFO * attr_info, HEAP_SCANCACHE * scancache, int unique_pk,
				 int ib_thread_count, TP_DOMAIN * key_type);
static bool btree_is_worker_pool_logging_true ();

typedef struct
{
  OID class_oid;		/* Class OID value in this sorted item */
  OID rec_oid;			/* OID value in this sorted item */
  MVCC_REC_HEADER mvcc_header;

  DB_VALUE this_key;		/* Key value in this sorted item (specified with in_recdes) */
  BTREE_MVCC_INFO mvcc_info;
  bool is_btree_ops_log;

  OID orig_oid;
  OID orig_class_oid;
  MVCC_REC_HEADER orig_mvcc_header;
} S_PARAM_ST;

static int bt_load_put_buf_to_record (RECDES * recdes, SORT_ARGS * sort_args, int value_has_null, OID * rec_oid,
				      MVCC_REC_HEADER * mvcc_header, DB_VALUE * dbvalue_ptr, int key_len,
				      int cur_class, bool is_btree_ops_log);
static int bt_load_get_buf_from_record (RECDES * recdes, LOAD_ARGS * load_args, S_PARAM_ST * pparam, bool copy);
static int bt_load_get_first_leaf_page_and_init_args (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args,
						      S_PARAM_ST * pparam);
static int bt_load_make_new_record_on_leaf_page (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, S_PARAM_ST * pparam,
						 int *sp_success);
static int bt_load_invalidate_mvcc_del_id (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, S_PARAM_ST * pparam);
static int bt_load_nospace_for_new_oid (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, int *sp_success);
static int bt_load_add_same_key_to_record (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, S_PARAM_ST * pparam,
					   int *sp_success);
static int bt_load_notify_to_vacuum (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, S_PARAM_ST * pparam,
				     char **notify_vacuum_rv_data, char *notify_vacuum_rv_data_bufalign);
static int bt_load_append_vacuum_notifications (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args);
static void bt_load_clear_vacuum_notifications (LOAD_ARGS * load_args);

/*
 * btree_get_node_header () -
 *
 *   return:
 *   page_ptr(in):
 *
 */
BTREE_NODE_HEADER *
btree_get_node_header (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr)
{
  RECDES header_record;
  BTREE_NODE_HEADER *header = NULL;

  assert (page_ptr != NULL);

#if !defined(NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, page_ptr, PAGE_BTREE);
#endif

  if (spage_get_record (thread_p, page_ptr, HEADER, &header_record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      return NULL;
    }

  header = (BTREE_NODE_HEADER *) header_record.data;
  if (header != NULL)
    {
      assert (header->max_key_len >= 0);
    }

  return header;
}

/*
 * btree_get_root_header () -
 *
 *   return:
 *   page_ptr(in):
 *
 */
BTREE_ROOT_HEADER *
btree_get_root_header (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr)
{
  RECDES header_record;
  BTREE_ROOT_HEADER *root_header = NULL;

  assert (page_ptr != NULL);

#if !defined(NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, page_ptr, PAGE_BTREE);
#endif

  if (spage_get_record (thread_p, page_ptr, HEADER, &header_record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      return NULL;
    }

  root_header = (BTREE_ROOT_HEADER *) header_record.data;
  if (root_header != NULL)
    {
      assert (root_header->node.node_level > 0);
      assert (root_header->node.max_key_len >= 0);
    }

  return root_header;
}

/*
 * btree_get_overflow_header () -
 *
 *   return:
 *   page_ptr(in):
 *
 */
BTREE_OVERFLOW_HEADER *
btree_get_overflow_header (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr)
{
  RECDES header_record;

  assert (page_ptr != NULL);

#if !defined(NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, page_ptr, PAGE_BTREE);
#endif

  if (spage_get_record (thread_p, page_ptr, HEADER, &header_record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      return NULL;
    }

  return (BTREE_OVERFLOW_HEADER *) header_record.data;
}

/*
 * btree_init_node_header () - insert btree node header
 *
 *   return:
 *   vfid(in):
 *   page_ptr(in):
 *   header(in):
 *
 */
int
btree_init_node_header (THREAD_ENTRY * thread_p, const VFID * vfid, PAGE_PTR page_ptr, BTREE_NODE_HEADER * header,
			bool redo)
{
  RECDES rec;
  char rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];

  assert (header != NULL);
  assert (header->node_level > 0);
  assert (header->max_key_len >= 0);

  /* create header record */
  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (rec_buf, BTREE_MAX_ALIGN);
  rec.type = REC_HOME;
  rec.length = sizeof (BTREE_NODE_HEADER);
  memcpy (rec.data, header, sizeof (BTREE_NODE_HEADER));

  if (redo == true)
    {
      /* log the new header record for redo purposes */
      log_append_redo_data2 (thread_p, RVBT_NDHEADER_INS, vfid, page_ptr, HEADER, rec.length, rec.data);
    }

  if (spage_insert_at (thread_p, page_ptr, HEADER, &rec) != SP_SUCCESS)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}


/*
 * btree_node_header_undo_log () -
 *
 *   return:
 *   vfid(in):
 *   page_ptr(in):
 *
 */
int
btree_node_header_undo_log (THREAD_ENTRY * thread_p, VFID * vfid, PAGE_PTR page_ptr)
{
  RECDES rec;
  if (spage_get_record (thread_p, page_ptr, HEADER, &rec, PEEK) != S_SUCCESS)
    {
      return ER_FAILED;
    }

  log_append_undo_data2 (thread_p, RVBT_NDHEADER_UPD, vfid, page_ptr, HEADER, rec.length, rec.data);

  return NO_ERROR;
}

/*
 * btree_node_header_redo_log () -
 *
 *   return:
 *   vfid(in):
 *   page_ptr(in):
 *
 */
int
btree_node_header_redo_log (THREAD_ENTRY * thread_p, VFID * vfid, PAGE_PTR page_ptr)
{
  RECDES rec;
  if (spage_get_record (thread_p, page_ptr, HEADER, &rec, PEEK) != S_SUCCESS)
    {
      return ER_FAILED;
    }

  log_append_redo_data2 (thread_p, RVBT_NDHEADER_UPD, vfid, page_ptr, HEADER, rec.length, rec.data);

  return NO_ERROR;
}

/*
 * btree_change_root_header_delta () -
 *
 *   return:
 *   vfid(in):
 *   page_ptr(in):
 *   null_delta(in):
 *   oid_delta(in):
 *   key_delta(in):
 *
 */
int
btree_change_root_header_delta (THREAD_ENTRY * thread_p, VFID * vfid, PAGE_PTR page_ptr, long long null_delta,
				long long oid_delta, long long key_delta)
{
  RECDES rec, delta_rec;
  char delta_rec_buf[(3 * OR_BIGINT_SIZE) + BTREE_MAX_ALIGN];
  BTREE_ROOT_HEADER *root_header = NULL;
  int old_version;

  delta_rec.data = NULL;
  delta_rec.area_size = 3 * OR_BIGINT_SIZE;
  delta_rec.data = PTR_ALIGN (delta_rec_buf, BTREE_MAX_ALIGN);

  if (spage_get_record (thread_p, page_ptr, HEADER, &rec, PEEK) != S_SUCCESS)
    {
      return ER_FAILED;
    }

  root_header = (BTREE_ROOT_HEADER *) rec.data;
  root_header->num_nulls += null_delta;
  root_header->num_oids += oid_delta;
  root_header->num_keys += key_delta;

  /* save root head for undo purposes */
  btree_rv_save_root_head (-null_delta, -oid_delta, -key_delta, &delta_rec);

  log_append_undoredo_data2 (thread_p, RVBT_ROOTHEADER_UPD, vfid, page_ptr, HEADER, delta_rec.length, rec.length,
			     delta_rec.data, rec.data);

  return NO_ERROR;
}


/*
 * btree_pack_root_header () -
 *   return:
 *   rec(out):
 *   root_header(in):
 *
 * Note: Writes the first record (header record) for a root page.
 * rec must be long enough to hold the header record.
 */
static int
btree_pack_root_header (RECDES * rec, BTREE_ROOT_HEADER * root_header, TP_DOMAIN * key_type)
{
  OR_BUF buf;
  int rc = NO_ERROR;
  int fixed_size = (int) offsetof (BTREE_ROOT_HEADER, packed_key_domain);

  memcpy (rec->data, root_header, fixed_size);

  or_init (&buf, rec->data + fixed_size, (rec->area_size == -1) ? -1 : (rec->area_size - fixed_size));

  rc = or_put_domain (&buf, key_type, 0, 0);

  rec->length = fixed_size + CAST_BUFLEN (buf.ptr - buf.buffer);
  rec->type = REC_HOME;

  if (rc != NO_ERROR && er_errid () == NO_ERROR)
    {
      /* if an error occurs then set a generic error so that at least an error to be send to client. */
      if (er_errid () == NO_ERROR)
	{
	  assert (false);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	}
    }

  return rc;
}

/*
 * btree_init_root_header () - insert btree node header
 *
 *   return:
 *   vfid(in):
 *   page_ptr(in):
 *   root_header(in):
 *
 */
int
btree_init_root_header (THREAD_ENTRY * thread_p, VFID * vfid, PAGE_PTR page_ptr, BTREE_ROOT_HEADER * root_header,
			TP_DOMAIN * key_type)
{
  RECDES rec;
  char copy_rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];

  assert (root_header != NULL);
  assert (root_header->node.node_level > 0);
  assert (root_header->node.max_key_len >= 0);

  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (copy_rec_buf, BTREE_MAX_ALIGN);

  if (btree_pack_root_header (&rec, root_header, key_type) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* insert the root header information into the root page */
  if (spage_insert_at (thread_p, page_ptr, HEADER, &rec) != SP_SUCCESS)
    {
      return ER_FAILED;
    }

  /* log the new header record for redo purposes */
  log_append_redo_data2 (thread_p, RVBT_NDHEADER_INS, vfid, page_ptr, HEADER, rec.length, rec.data);

  return NO_ERROR;
}

/*
 * btree_init_overflow_header () - insert btree overflow node header
 *
 *   return:
 *   page_ptr(in):
 *   ovf_header(in):
 *
 */
int
btree_init_overflow_header (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr, BTREE_OVERFLOW_HEADER * ovf_header)
{
  RECDES rec;
  char copy_rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];

  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (copy_rec_buf, BTREE_MAX_ALIGN);
  memcpy (rec.data, ovf_header, sizeof (BTREE_OVERFLOW_HEADER));
  rec.length = sizeof (BTREE_OVERFLOW_HEADER);
  rec.type = REC_HOME;

  /* insert the root header information into the root page */
  if (spage_insert_at (thread_p, page_ptr, HEADER, &rec) != SP_SUCCESS)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * btree_rv_save_root_head () - Save root head stats FOR LOGICAL LOG PURPOSES
 *   return:
 *   null_delta(in):
 *   oid_delta(in):
 *   key_delta(in):
 *   recdes(out):
 *
 * Note: Copy the root header statistics to the data area provided.
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine.
 */
static void
btree_rv_save_root_head (long long null_delta, long long oid_delta, long long key_delta, RECDES * recdes)
{
  char *datap;

  assert (recdes->area_size >= 3 * OR_BIGINT_SIZE);

  recdes->length = 0;
  datap = (char *) recdes->data;
  OR_PUT_BIGINT (datap, &null_delta);
  datap += OR_BIGINT_SIZE;
  OR_PUT_BIGINT (datap, &oid_delta);
  datap += OR_BIGINT_SIZE;
  OR_PUT_BIGINT (datap, &key_delta);
  datap += OR_BIGINT_SIZE;

  recdes->length = CAST_STRLEN (datap - recdes->data);
}

/*
 * btree_rv_mvcc_save_increments () - Save unique_stats
 *   return:
 *   btid(in):
 *   max_key_len(in):
 *   null_delta(in):
 *   oid_delta(in):
 *   key_delta(in):
 *   recdes(in):
 *
 * Note: Copy the unique statistics to the data area provided.
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine.
 */
void
btree_rv_mvcc_save_increments (const BTID * btid, long long key_delta, long long oid_delta, long long null_delta,
			       RECDES * recdes)
{
  char *datap;

  assert (recdes != NULL && (recdes->area_size >= ((3 * OR_BIGINT_SIZE) + OR_BTID_ALIGNED_SIZE)));

  recdes->length = (3 * OR_BIGINT_SIZE) + OR_BTID_ALIGNED_SIZE;
  datap = (char *) recdes->data;

  OR_PUT_BTID (datap, btid);
  datap += OR_BTID_ALIGNED_SIZE;

  OR_PUT_BIGINT (datap, &key_delta);
  datap += OR_BIGINT_SIZE;

  OR_PUT_BIGINT (datap, &oid_delta);
  datap += OR_BIGINT_SIZE;

  OR_PUT_BIGINT (datap, &null_delta);
  datap += OR_BIGINT_SIZE;

  recdes->length = CAST_STRLEN (datap - recdes->data);
}

/*
 * btree_get_next_overflow_vpid () -
 *
 *   return:
 *   page_ptr(in):
 *
 */
int
btree_get_next_overflow_vpid (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr, VPID * vpid)
{
  BTREE_OVERFLOW_HEADER *ovf_header = NULL;

  ovf_header = btree_get_overflow_header (thread_p, page_ptr);
  if (ovf_header == NULL)
    {
      return ER_FAILED;
    }

  *vpid = ovf_header->next_vpid;

  return NO_ERROR;
}

int
bt_load_heap_scancache_start_for_attrinfo (THREAD_ENTRY * thread_p, SORT_ARGS * args, HEAP_SCANCACHE * scan_cache,
					   HEAP_CACHE_ATTRINFO * attr_info, int save_cache_last_fix_page)
{
  int attr_offset = args->cur_class * args->n_attrs;

  if (scan_cache == NULL)
    {
      scan_cache = &args->hfscan_cache;
    }
  if (attr_info == NULL)
    {
      attr_info = &args->attr_info;
    }

  /* Start scancache */
  if (heap_scancache_start (thread_p, scan_cache, &args->hfids[args->cur_class],
			    &args->class_ids[args->cur_class], save_cache_last_fix_page, NULL) != NO_ERROR)
    {
      return ER_FAILED;
    }
  args->scancache_inited = true;

  if (heap_attrinfo_start (thread_p, &args->class_ids[args->cur_class], args->n_attrs,
			   &args->attr_ids[attr_offset], attr_info) != NO_ERROR)
    {
      return ER_FAILED;
    }
  args->attrinfo_inited = true;

  if (args->filter != NULL)
    {
      if (heap_attrinfo_start (thread_p, &args->class_ids[args->cur_class], args->filter->num_attrs_pred,
			       args->filter->attrids_pred, args->filter->cache_pred) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  if (args->func_index_info && args->func_index_info->expr)
    {
      if (heap_attrinfo_start (thread_p, &args->class_ids[args->cur_class], args->n_attrs,
			       &args->attr_ids[attr_offset], args->func_index_info->expr->cache_attrinfo) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

void
bt_load_heap_scancache_end_for_attrinfo (THREAD_ENTRY * thread_p, SORT_ARGS * args, HEAP_SCANCACHE * scan_cache,
					 HEAP_CACHE_ATTRINFO * attr_info)
{
  if (scan_cache == NULL)
    {
      scan_cache = &args->hfscan_cache;
    }
  if (attr_info == NULL)
    {
      attr_info = &args->attr_info;
    }

  if (args->attrinfo_inited)
    {
      heap_attrinfo_end (thread_p, attr_info);
      if (args->filter)
	{
	  heap_attrinfo_end (thread_p, args->filter->cache_pred);
	}
      if (args->func_index_info && args->func_index_info->expr)
	{
	  heap_attrinfo_end (thread_p, args->func_index_info->expr->cache_attrinfo);
	}
      args->attrinfo_inited = false;
    }

  if (args->scancache_inited)
    {
      (void) heap_scancache_end (thread_p, scan_cache);
      args->scancache_inited = false;
    }
}

void
bt_load_clear_pred_and_unpack (THREAD_ENTRY * thread_p, SORT_ARGS * args, XASL_UNPACK_INFO * func_unpack_info)
{
  if (args->filter != NULL)
    {
      /* to clear db values from dbvalue regu variable */
      qexec_clear_pred_context (thread_p, args->filter, true);

      if (args->filter->unpack_info != NULL)
	{
	  free_xasl_unpack_info (thread_p, args->filter->unpack_info);
	}
      db_private_free_and_init (thread_p, args->filter);
    }

  if (args->func_index_info && args->func_index_info->expr != NULL)
    {
      (void) qexec_clear_func_pred (thread_p, args->func_index_info->expr);
      args->func_index_info->expr = NULL;
    }

  if (func_unpack_info != NULL)
    {
      free_xasl_unpack_info (thread_p, func_unpack_info);
    }
}

int
btree_load_filter_pred_function_info (THREAD_ENTRY * thread_p, SORT_ARGS * sort_args,
				      PRED_EXPR_WITH_CONTEXT ** filter_pred_p, FILTER_INDEX_INFO * filter_index_info_p,
				      FUNCTION_INDEX_INFO * func_index_info_p, XASL_UNPACK_INFO ** func_unpack_info_p,
				      DB_TYPE * single_node_type)
{
  if (filter_index_info_p && filter_index_info_p->pred_stream && filter_index_info_p->pred_stream_size > 0)
    {
      if (stx_map_stream_to_filter_pred (thread_p, filter_pred_p, filter_index_info_p->pred_stream,
					 filter_index_info_p->pred_stream_size) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      sort_args->filter = *filter_pred_p;
      sort_args->filter_eval_func = eval_fnc (thread_p, (*filter_pred_p)->pred, single_node_type);
    }

  if (func_index_info_p && func_index_info_p->expr_stream && func_index_info_p->expr_stream_size > 0)
    {
      if (stx_map_stream_to_func_pred (thread_p, &func_index_info_p->expr, func_index_info_p->expr_stream,
				       func_index_info_p->expr_stream_size, func_unpack_info_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      sort_args->func_index_info = func_index_info_p;
    }

  return NO_ERROR;
}


/* upper bound of distinct volumes one build's files may span before the scoped barrier gives up and the caller
 * falls back to the global barrier (never a durability loss, only a lost optimization). */
#define BT_LOAD_SCOPED_BARRIER_MAX_VOLS 64

/*
 * bt_load_scoped_barrier_flush_file () - flush every still-buffered, still-dirty data page of one bulk-build file
 *					  and collect the distinct volume ids its sectors live on.
 *   return: error code (any error: caller falls back to the global barrier)
 *   thread_p(in): thread entry
 *   vfid(in): file whose allocated data pages must be made durable
 *   volids(in/out): distinct volume id accumulator, shared across the build's files
 *   volids_capacity(in): capacity of volids
 *   n_volids(in/out): number of collected distinct volume ids
 *   pages_flushed(in/out): evidence counter, incremented per page submitted to flush
 *   n_sectors(in/out): evidence counter, incremented by the file's data sector count
 *
 * Note: file_get_all_data_sectors returns every partial/full sector of the file with per-page allocation bitmaps,
 *       under the fhead read latch; file table pages are masked out of the bitmaps -- file bookkeeping is always
 *       WAL-logged, even for a no-redo bulk build (log_recovery_bulk_redo_skip_is_exempt) -- so every set bit is
 *       an allocated data page and every no-redo content page of the file has its bit set.
 */
static int
bt_load_scoped_barrier_flush_file (THREAD_ENTRY * thread_p, const VFID * vfid, VOLID * volids, int volids_capacity,
				   int *n_volids, int *pages_flushed, int *n_sectors)
{
  FILE_FTAB_COLLECTOR collector = FILE_FTAB_COLLECTOR_INITIALIZER;
  int error = NO_ERROR;
  int i, k;

  assert (vfid != NULL && !VFID_ISNULL (vfid));

  error = file_get_all_data_sectors (thread_p, vfid, &collector);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      if (collector.partsect_ftab != NULL)
	{
	  db_private_free_and_init (thread_p, collector.partsect_ftab);
	}
      return error;
    }

  for (i = 0; i < collector.nsects; i++)
    {
      FILE_PARTIAL_SECTOR *partsect = &collector.partsect_ftab[i];
      VPID vpid;
      int iter;

      /* collect the sector's volume id; every page of a sector lives on the sector's volume. */
      for (k = 0; k < *n_volids; k++)
	{
	  if (volids[k] == partsect->vsid.volid)
	    {
	      break;
	    }
	}
      if (k == *n_volids)
	{
	  if (*n_volids == volids_capacity)
	    {
	      /* build spans absurdly many volumes; fall back to the global barrier. */
	      db_private_free_and_init (thread_p, collector.partsect_ftab);
	      return ER_FAILED;
	    }
	  volids[(*n_volids)++] = partsect->vsid.volid;
	}

      if (partsect->page_bitmap == FILE_EMPTY_PAGE_BITMAP)
	{
	  continue;
	}

      vpid.volid = partsect->vsid.volid;
      for (iter = 0, vpid.pageid = SECTOR_FIRST_PAGEID (partsect->vsid.sectid); iter < FILE_ALLOC_BITMAP_NBITS;
	   iter++, vpid.pageid++)
	{
	  bool flushed = false;

	  if ((partsect->page_bitmap & (((FILE_ALLOC_BITMAP) 1) << iter)) == 0)
	    {
	      /* not allocated, or a WAL-logged file table page masked out by file_get_all_data_sectors. */
	      continue;
	    }
	  error = pgbuf_flush_page_if_exists_and_dirty (thread_p, &vpid, &flushed);
	  if (error != NO_ERROR)
	    {
	      db_private_free_and_init (thread_p, collector.partsect_ftab);
	      return error;
	    }
	  if (flushed)
	    {
	      (*pages_flushed)++;
	    }
	}
    }

  *n_sectors += collector.nsects;
  db_private_free_and_init (thread_p, collector.partsect_ftab);
  return NO_ERROR;
}

/*
 * bt_load_scoped_durability_barrier () - flush + sync exactly the files the no-redo build owns, instead of the
 *					  whole buffer pool and every permanent volume.
 *   return: error code (any error: the caller MUST fall back to the global barrier)
 *   thread_p(in): thread entry
 *   load_args(in): load args of the finished build (main b-tree file and optional overflow key file)
 *
 * Note: completeness argument, in short: (1) every no-redo content page is a data page of {btid->vfid,
 *       btid->ovfid} -- all btree_log_page callers target one of the two files and the serial overflow-key path
 *       (overflow_insert) logs full content; (2) every allocated data page of a file appears in its partial/full
 *       sector tables (file_manager reserve-then-allocate invariant); (3) this runs single-threaded after the
 *       parallel workers were joined and after the root publish, and nothing allocates before the publication
 *       chain; (4) a probe miss means the page was flushed before it was victimized (pgbuf invariant), so
 *       flushing the still-buffered dirty subset + one DWB drain + fsync of the owning volumes makes every
 *       content page durable before the RVBT_BULK_BUILD_DURABLE marker can become durable.
 */
static int
bt_load_scoped_durability_barrier (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args)
{
  VOLID volids[BT_LOAD_SCOPED_BARRIER_MAX_VOLS];
  int n_volids = 0;
  int pages_flushed = 0;
  int n_sectors = 0;
  int n_files = 1;
  int error = NO_ERROR;
  bool all_sync = false;
  int i;

  /* 1. scoped flush: the main b-tree file, then the overflow key file when one was created and kept.  (When the
   * pre-created overflow key file went unused, it was already postpone-destroyed and btid->ovfid nulled before
   * the root publish.) */
  error = bt_load_scoped_barrier_flush_file (thread_p, &load_args->btid->sys_btid->vfid, volids,
					     BT_LOAD_SCOPED_BARRIER_MAX_VOLS, &n_volids, &pages_flushed, &n_sectors);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (!VFID_ISNULL (&load_args->btid->ovfid))
    {
      n_files++;
      error = bt_load_scoped_barrier_flush_file (thread_p, &load_args->btid->ovfid, volids,
						 BT_LOAD_SCOPED_BARRIER_MAX_VOLS, &n_volids, &pages_flushed,
						 &n_sectors);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* 2. scoped sync: drain the DWB once per build (no-op when disabled), then fsync the volumes owning the files'
   * sectors.  Same structure as fileio_synchronize_all: when the drain reports every touched volume already
   * synchronized, the explicit fsync pass is redundant and skipped.  ensure_metadata == true (fsync, not
   * fdatasync) intentionally: a mid-build volume extension changes file sizes. */
  error = dwb_flush_force (thread_p, &all_sync);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (!all_sync)
    {
      for (i = 0; i < n_volids; i++)
	{
	  int vdes = fileio_get_volume_descriptor (volids[i]);

	  if (vdes == NULL_VOLDES
	      || fileio_synchronize (thread_p, vdes, fileio_get_volume_label (volids[i], PEEK), true) == NULL_VOLDES)
	    {
	      return ER_FAILED;
	    }
	}
    }

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "DEBUG_BTREE: scoped barrier btid(%d, (%d, %d)): files=%d sectors=%d pages_flushed=%d "
		     "n_volids=%d dwb_all_sync=%d.", load_args->btid->sys_btid->root_pageid,
		     load_args->btid->sys_btid->vfid.volid, load_args->btid->sys_btid->vfid.fileid, n_files,
		     n_sectors, pages_flushed, n_volids, all_sync ? 1 : 0);
      for (i = 0; i < n_volids; i++)
	{
	  _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: scoped barrier btid(%d, (%d, %d)): sync volid=%d.",
			 load_args->btid->sys_btid->root_pageid, load_args->btid->sys_btid->vfid.volid,
			 load_args->btid->sys_btid->vfid.fileid, (int) volids[i]);
	}
    }

  return NO_ERROR;
}

/*
 * xbtree_load_index () - create & load b+tree index
 *   return: BTID * (btid on success and NULL on failure)
 *           btid is set as a side effect.
 *   btid(out):
 *      btid: Set to the created B+tree index identifier
 *            (Note: btid->vfid.volid should be set by the caller)
 *   bt_name(in): index name
 *   key_type(in): Key type corresponding to the attribute.
 *   class_oids(in): OID of the class for which the index will be created
 *   n_classes(in):
 *   n_attrs(in):
 *   attr_ids(in): Identifier of the attribute of the class for which the index
 *                 will be created.
 *   hfids(in): Identifier of the heap file containing the instances of the
 *	        class
 *   unique_pk(in):
 *   not_null_flag(in):
 *   fk_refcls_oid(in):
 *   fk_refcls_pk_btid(in):
 *   fk_name(in):
 *
 */
BTID *
xbtree_load_index (THREAD_ENTRY * thread_p, BTID * btid, const char *bt_name, TP_DOMAIN * key_type, OID * class_oids,
		   int n_classes, int n_attrs, int *attr_ids, int *attrs_prefix_length, HFID * hfids, int unique_pk,
		   int not_null_flag, OID * fk_refcls_oid, BTID * fk_refcls_pk_btid, const char *fk_name,
		   char *pred_stream, int pred_stream_size, char *func_pred_stream, int func_pred_stream_size,
		   int func_col_id, int func_attr_index_start, bool eligible_no_redo, LOG_LSA * create_lsa)
{
  LOG_TDES *tdes = NULL;
  SORT_ARGS sort_args_info, *sort_args;
  LOAD_ARGS load_args_info, *load_args;
  int n_ovf_keys = 0;
  INT64 sum_ovf_pages = 0;
  BTID_INT btid_int;
  PRED_EXPR_WITH_CONTEXT *filter_pred = NULL;
#if defined (SERVER_MODE)
  FILTER_INDEX_INFO filter_index_info;
#endif /* SERVER_MODE */
  FUNCTION_INDEX_INFO func_index_info;
  DB_TYPE single_node_type = DB_TYPE_NULL;
  XASL_UNPACK_INFO *func_unpack_info = NULL;
  bool has_fk;
  BTID btid_global_stats = BTID_INITIALIZER;
  OID *notification_class_oid;
  bool is_sysop_started = false;
  bool built_bulk_tree = false;
  if (create_lsa != NULL)
    {
      LSA_SET_NULL (create_lsa);
    }

  /* Check for robustness */
  if (!btid || !hfids || !class_oids || !attr_ids || !key_type)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_LOAD_FAILED, 0);
      return NULL;
    }

  sort_args = &sort_args_info;
  load_args = &load_args_info;

  /* Initialize pointers which might be used in case of error */
  load_args->nleaf.pgptr = NULL;
  load_args->leaf.pgptr = NULL;
  load_args->ovf.pgptr = NULL;
  load_args->leaf_nleaf_recdes.data = NULL;
  load_args->ovf_recdes.data = NULL;
  load_args->out_recdes = NULL;
  load_args->push_list = NULL;
  load_args->pop_list = NULL;
  load_args->build_mvccid = MVCCID_NULL;
  load_args->provider = NULL;
  load_args->worker_idx = -1;
  load_args->new_page_fn = bt_load_new_page_serial;
  load_args->px_outcome = BT_PX_NOT_ATTEMPTED;
  load_args->sort_args = sort_args;
  load_args->report_local_max_key_len = 0;
  load_args->report_n_keys = 0;
  load_args->report_n_oids = 0;
  load_args->report_n_nulls = 0;
  load_args->report_overflow_key_count = 0;
  load_args->report_first_error = NO_ERROR;
  load_args->report_published = false;
  VPID_SET_NULL (&load_args->report_first_leaf_vpid);
  VPID_SET_NULL (&load_args->report_last_leaf_vpid);
  load_args->vacuum_items = NULL;
  load_args->vacuum_count = 0;
  load_args->vacuum_capacity = 0;
#if defined (SERVER_MODE)
  load_args->no_redo = eligible_no_redo;
  if (!load_args->no_redo)
    {
      log_sysop_start (thread_p);
      is_sysop_started = true;
    }
#else
  load_args->no_redo = false;
#endif
  load_args->vacuum_payload_size = 0;

  /*
   * Start a TOP SYSTEM OPERATION.
   * This top system operation will be either ABORTED (case of failure) or
   * COMMITTED, so that the new file becomes kind of permanent.  This allows
   * us to make use of un-used pages in the case of a bad init_pgcnt guess.
   */

#if !defined (SERVER_MODE)
  log_sysop_start (thread_p);
  is_sysop_started = true;
#endif /* !SERVER_MODE */

  thread_p->push_resource_tracks ();

  btid_int.sys_btid = btid;
  btid_int.unique_pk = unique_pk;
#if !defined(NDEBUG)
  if (unique_pk)
    {
      assert (BTREE_IS_UNIQUE (btid_int.unique_pk));
      assert (BTREE_IS_PRIMARY_KEY (btid_int.unique_pk) || !BTREE_IS_PRIMARY_KEY (btid_int.unique_pk));
    }
#endif
  btid_int.key_type = key_type;
  VFID_SET_NULL (&btid_int.ovfid);
  btid_int.rev_level = BTREE_CURRENT_REV_LEVEL;
  /* support for SUPPORT_DEDUPLICATE_KEY_MODE */
  btid_int.deduplicate_key_idx = dk_get_deduplicate_key_position (n_attrs, attr_ids, func_attr_index_start);

  COPY_OID (&btid_int.topclass_oid, &class_oids[0]);

  /*
   * for btree_range_search, part_key_desc is re-set at btree_initialize_bts
   */
  btid_int.part_key_desc = 0;

  /* init index key copy_buf info */
  btid_int.copy_buf = NULL;
  btid_int.copy_buf_len = 0;

  btid_int.nonleaf_key_type = btree_generate_prefix_domain (&btid_int);

  /* Initialize the fields of sorting argument structures */
  sort_args->oldest_visible_mvccid = log_Gl.mvcc_table.get_global_oldest_visible ();
  sort_args->unique_pk = unique_pk;
  sort_args->not_null_flag = not_null_flag;
  sort_args->hfids = hfids;
  sort_args->class_ids = class_oids;
  sort_args->attr_ids = attr_ids;
  sort_args->n_attrs = n_attrs;
  sort_args->attrs_prefix_length = attrs_prefix_length;
  sort_args->n_classes = n_classes;
  sort_args->key_type = key_type;
  OID_SET_NULL (&sort_args->cur_oid);
  sort_args->n_nulls = 0;
  sort_args->n_oids = 0;
  sort_args->cur_class = 0;
  sort_args->scancache_inited = false;
  sort_args->attrinfo_inited = false;
  sort_args->btid = &btid_int;
  sort_args->n_ovf_keys = n_ovf_keys;
  sort_args->sum_ovf_pages = sum_ovf_pages;
  sort_args->fk_refcls_oid = fk_refcls_oid;
  sort_args->fk_refcls_pk_btid = fk_refcls_pk_btid;
  sort_args->fk_name = fk_name;
  sort_args->filter_index_info = NULL;
  if (pred_stream && pred_stream_size > 0)
    {
      if (stx_map_stream_to_filter_pred (thread_p, &filter_pred, pred_stream, pred_stream_size) != NO_ERROR)
	{
	  goto error;
	}
#if defined (SERVER_MODE)
      filter_index_info.pred_stream = pred_stream;
      filter_index_info.pred_stream_size = pred_stream_size;
      sort_args->filter_index_info = &filter_index_info;
#endif /* SERVER_MODE */
    }
  sort_args->filter = filter_pred;
  sort_args->filter_eval_func = (filter_pred) ? eval_fnc (thread_p, filter_pred->pred, &single_node_type) : NULL;
  sort_args->func_index_info = NULL;
  if (func_pred_stream && func_pred_stream_size > 0)
    {
      func_index_info.expr_stream = func_pred_stream;
      func_index_info.expr_stream_size = func_pred_stream_size;
      func_index_info.col_id = func_col_id;
      func_index_info.attr_index_start = func_attr_index_start;
      func_index_info.expr = NULL;
      if (stx_map_stream_to_func_pred (thread_p, &func_index_info.expr, func_pred_stream,
				       func_pred_stream_size, &func_unpack_info))
	{
	  goto error;
	}
      sort_args->func_index_info = &func_index_info;
    }

  /*
   * Start a heap scan cache for reading objects using the first nun-null heap
   * We are guaranteed that such a heap exists, otherwise btree_load_index
   * would not have been called.
   */
  while (sort_args->cur_class < sort_args->n_classes && HFID_IS_NULL (&sort_args->hfids[sort_args->cur_class]))
    {
      sort_args->cur_class++;
    }

  /* After building index acquire lock on table, the transaction has deadlock priority */
  tdes = LOG_FIND_CURRENT_TDES (thread_p);
  if (tdes)
    {
      tdes->has_deadlock_priority = true;
    }

  /* Start scancache */
  has_fk = (fk_refcls_oid != NULL && !OID_ISNULL (fk_refcls_oid));

#if !defined (SERVER_MODE)
  if (bt_load_heap_scancache_start_for_attrinfo (thread_p, sort_args, NULL, NULL, !has_fk) != NO_ERROR)
    {
      goto error;
    }

  if (btree_create_file (thread_p, &class_oids[0], attr_ids[0], btid) != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error;
    }

  /* if loading is aborted or if transaction is aborted, vacuum must be notified before file is destoyed. */
  vacuum_log_add_dropped_file (thread_p, &btid->vfid, NULL, VACUUM_LOG_ADD_DROPPED_FILE_UNDO);
#endif /* !SERVER_MODE */

  /** Initialize the fields of loading argument structures **/
  load_args->btid = &btid_int;
  load_args->bt_name = bt_name;
  db_make_null (&load_args->current_key);
  VPID_SET_NULL (&load_args->nleaf.vpid);
  load_args->nleaf.pgptr = NULL;
  VPID_SET_NULL (&load_args->leaf.vpid);
  load_args->leaf.pgptr = NULL;
  VPID_SET_NULL (&load_args->ovf.vpid);
  load_args->ovf.pgptr = NULL;
  load_args->n_keys = 0;
  load_args->curr_non_del_obj_count = 0;

  load_args->leaf_nleaf_recdes.area_size = BTREE_MAX_KEYLEN_INPAGE + BTREE_MAX_OIDLEN_INPAGE;
  load_args->leaf_nleaf_recdes.length = 0;
  load_args->leaf_nleaf_recdes.type = REC_HOME;
  load_args->leaf_nleaf_recdes.data = (char *) os_malloc (load_args->leaf_nleaf_recdes.area_size);
  if (load_args->leaf_nleaf_recdes.data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, load_args->leaf_nleaf_recdes.area_size);
      goto error;
    }
  load_args->ovf_recdes.area_size = DB_PAGESIZE;
  load_args->ovf_recdes.length = 0;
  load_args->ovf_recdes.type = REC_HOME;
  load_args->ovf_recdes.data = (char *) os_malloc (load_args->ovf_recdes.area_size);
  if (load_args->ovf_recdes.data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, load_args->ovf_recdes.area_size);
      goto error;
    }
  load_args->out_recdes = NULL;

  /* Allocate a root page and save the page_id */
  *load_args->btid->sys_btid = *btid;
  btid_global_stats = *btid;

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: load start on class(%d, %d, %d), btid(%d, (%d, %d)).",
		     sort_args->class_ids[sort_args->cur_class].volid,
		     sort_args->class_ids[sort_args->cur_class].pageid,
		     sort_args->class_ids[sort_args->cur_class].slotid, sort_args->btid->sys_btid->root_pageid,
		     sort_args->btid->sys_btid->vfid.volid, sort_args->btid->sys_btid->vfid.fileid);
    }

#if defined (SERVER_MODE)
  load_args->build_mvccid = logtb_get_current_mvccid (thread_p);
#endif
  /* Build the leaf pages of the btree as the output of the sort. */
  if (btree_index_sort (thread_p, sort_args, btree_construct_leafs, load_args) != NO_ERROR)
    {
      goto error;
    }
#if defined (SERVER_MODE) && !defined (NDEBUG)
  assert (!load_args->no_redo || tdes == NULL
	  || (load_args->px_outcome == BT_PX_NOT_ATTEMPTED ? tdes->topops.last >= 0 : tdes->topops.last < 0));
#endif

#if defined (SERVER_MODE)
  if (!load_args->no_redo || load_args->px_outcome == BT_PX_NOT_ATTEMPTED)
    {
      /* Legacy builds and the no-redo serial fallback leave a build sysop for this layer to attach. */
      is_sysop_started = log_check_system_op_is_started (thread_p);
    }
  if (create_lsa != NULL && tdes != NULL)
    {
      /* This is the lower bound for media-recovery cleanup; later vacuum WAL must not move it. */
      LSA_COPY (create_lsa, &tdes->tail_lsa);
    }
#endif /* SERVER_MODE */

  if (bt_load_append_vacuum_notifications (thread_p, load_args) != NO_ERROR)
    {
      goto error;
    }
  bt_load_clear_vacuum_notifications (load_args);

#if !defined (SERVER_MODE)
  bt_load_heap_scancache_end_for_attrinfo (thread_p, sort_args, NULL, NULL);
#endif /* !SERVER_MODE */

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "DEBUG_BTREE: load finished all. %d classes loaded, found %d nulls and %d oids, "
		     "load %d keys.", sort_args->n_classes, sort_args->n_nulls, sort_args->n_oids, load_args->n_keys);
    }

  /* Just to make sure that there were entries to put into the tree */
  if (load_args->px_outcome == BT_PX_TREE_DONE)
    {
      built_bulk_tree = true;
      if (has_fk && btree_load_check_fk (thread_p, load_args, sort_args) != NO_ERROR)
	{
	  goto error;
	}
      os_free_and_init (load_args->leaf_nleaf_recdes.data);
      os_free_and_init (load_args->ovf_recdes.data);
      pr_clear_value (&load_args->current_key);
#if !defined(NDEBUG)
      (void) btree_verify_tree (thread_p, &class_oids[0], &btid_int, bt_name);
#endif
    }
  else if (load_args->leaf.pgptr != NULL)
    {
      built_bulk_tree = true;
      /* Save the last leaf record */

      if (btree_save_last_leafrec (thread_p, load_args) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_LOAD_FAILED, 0);
	  goto error;
	}

      /* No need to deal with overflow pages anymore */
      load_args->ovf.pgptr = NULL;

      /* Check the correctness of the foreign key, if any. */
      if (has_fk)
	{
	  if (btree_load_check_fk (thread_p, load_args, sort_args) != NO_ERROR)
	    {
	      goto error;
	    }
	}

      /* Build the non leaf nodes of the btree; Root page id will be assigned here */

      if (btree_build_nleafs (thread_p, load_args, sort_args->n_nulls, sort_args->n_oids, load_args->n_keys) !=
	  NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_LOAD_FAILED, 0);
	  goto error;
	}

      /* There is at least one leaf page */

      /* Release the memory area */
      os_free_and_init (load_args->leaf_nleaf_recdes.data);
      os_free_and_init (load_args->ovf_recdes.data);
      pr_clear_value (&load_args->current_key);

      if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
	{
	  _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: load built index btid (%d, (%d, %d)).",
			 btid_int.sys_btid->root_pageid, btid_int.sys_btid->vfid.volid, btid_int.sys_btid->vfid.fileid);
	}

#if !defined(NDEBUG)
      (void) btree_verify_tree (thread_p, &class_oids[0], &btid_int, bt_name);
#endif
    }
  else
    {
      if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
	{
	  _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: load didn't build any leaves btid (%d, (%d, %d)).",
			 btid_int.sys_btid->root_pageid, btid_int.sys_btid->vfid.volid, btid_int.sys_btid->vfid.fileid);
	}
      /* redo an empty index, but first destroy the one we created. the safest way is to abort changes so far. */
      log_sysop_abort (thread_p);
      is_sysop_started = false;

      os_free_and_init (load_args->leaf_nleaf_recdes.data);
      os_free_and_init (load_args->ovf_recdes.data);
      pr_clear_value (&load_args->current_key);

      BTID_SET_NULL (btid);
      if (xbtree_add_index (thread_p, btid, key_type, &class_oids[0], attr_ids[0], unique_pk, sort_args->n_oids,
			    sort_args->n_nulls, load_args->n_keys, btid_int.deduplicate_key_idx) == NULL)
	{
	  goto error;
	}
    }

  if (!VFID_ISNULL (&load_args->btid->ovfid))
    {
      /* notification */
      if (!OID_ISNULL (&class_oids[0]))
	{
	  notification_class_oid = &class_oids[0];
	}
      else
	{
	  notification_class_oid = &btid_int.topclass_oid;
	}
      BTREE_SET_CREATED_OVERFLOW_KEY_NOTIFICATION (thread_p, NULL, NULL, notification_class_oid, btid, bt_name);
    }

  if (load_args->no_redo)
    {
      /*
       * Durability barrier.  Bulk pages are only marked dirty during the build (their
       * content is never WAL-logged), so before the publication chain (sticky-root COPYPAGE,
       * sysop attach, RVBT_BULK_BUILD_DURABLE marker, commit) can become durable, every bulk
       * page must actually be ON DISK.
       *
       * Scoped mode: every no-redo content page is a data page of the build's own files
       * ({btid->vfid, btid->ovfid}), so it suffices to flush the still-dirty buffered pages
       * of those files (enumerated from their partial/full sector tables), drain the DWB
       * once, and fsync only the volumes owning the files' sectors.  Any scoped failure
       * falls back to the global barrier (pgbuf_flush_all + fileio_synchronize_all); so
       * does the empty-index path (built_bulk_tree == false: the bulk file was destroyed by
       * sysop abort and the replacement empty index is fully WAL-logged) and the
       * bulk_build_scoped_barrier=no kill switch.  In both modes the order is: pool flush
       * first, DWB drain + fsync second, publication chain only after success.
       */
      bool scoped_done = false;

      if (built_bulk_tree && prm_get_bool_value (PRM_ID_BULK_BUILD_SCOPED_BARRIER))
	{
	  er_stack_push ();
	  if (bt_load_scoped_durability_barrier (thread_p, load_args) == NO_ERROR)
	    {
	      scoped_done = true;
	    }
	  else
	    {
	      _er_log_debug (ARG_FILE_LINE,
			     "scoped barrier failed (error = %d); falling back to the global barrier.", er_errid ());
	    }
	  er_stack_pop ();
	}
      if (!scoped_done)
	{
	  if (pgbuf_flush_all (thread_p, NULL_VOLID) != NO_ERROR)
	    {
	      goto error;
	    }
	  if (fileio_synchronize_all (thread_p) != NO_ERROR)
	    {
	      goto error;
	    }
	}
    }

  bt_load_clear_pred_and_unpack (thread_p, sort_args, func_unpack_info);

  thread_p->pop_resource_tracks ();

  if (is_sysop_started)
    {
      /* todo: we have the option to commit & undo here. on undo, we can destroy the file directly. */
      log_sysop_attach_to_outer (thread_p);
    }
  if (unique_pk && (is_sysop_started || load_args->px_outcome == BT_PX_TREE_DONE))
    {
      /* drop statistics if aborted */
      log_append_undo_data2 (thread_p, RVBT_REMOVE_UNIQUE_STATS, NULL, NULL, NULL_OFFSET, sizeof (BTID), btid);
    }

  logpb_force_flush_pages (thread_p);

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE, "BTREE_DEBUG: load finished successful index btid(%d, (%d, %d)).",
		     btid_int.sys_btid->root_pageid, btid_int.sys_btid->vfid.volid, btid_int.sys_btid->vfid.fileid);
    }

  return btid;

error:

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE, "BTREE_DEBUG: load aborted index btid(%d, (%d, %d)).",
		     btid_int.sys_btid->root_pageid, btid_int.sys_btid->vfid.volid, btid_int.sys_btid->vfid.fileid);
    }

  if (!BTID_IS_NULL (&btid_global_stats))
    {
      logtb_delete_global_unique_stats (thread_p, &btid_global_stats);
    }

  bt_load_heap_scancache_end_for_attrinfo (thread_p, sort_args, NULL, NULL);

  VFID_SET_NULL (&btid->vfid);
  btid->root_pageid = NULL_PAGEID;
  if (load_args->leaf_nleaf_recdes.data)
    {
      os_free_and_init (load_args->leaf_nleaf_recdes.data);
    }
  if (load_args->ovf_recdes.data)
    {
      os_free_and_init (load_args->ovf_recdes.data);
    }
  pr_clear_value (&load_args->current_key);
  bt_load_clear_vacuum_notifications (load_args);

  pgbuf_unfix_and_init_after_check (thread_p, load_args->leaf.pgptr);
  pgbuf_unfix_and_init_after_check (thread_p, load_args->ovf.pgptr);
  pgbuf_unfix_and_init_after_check (thread_p, load_args->nleaf.pgptr);

  if (load_args->push_list != NULL)
    {
      list_clear (load_args->push_list);
      load_args->push_list = NULL;
    }
  if (load_args->pop_list != NULL)
    {
      list_clear (load_args->pop_list);
      load_args->pop_list = NULL;
    }

  bt_load_clear_pred_and_unpack (thread_p, sort_args, func_unpack_info);

  thread_p->pop_resource_tracks ();

  if (is_sysop_started)
    {
      log_sysop_abort (thread_p);
    }

  return NULL;
}

/*
 * btree_save_last_leafrec () - save the last leaf record
 *   return: NO_ERROR
 *   load_args(in): Collection of info. for btree load operation
 *
 * Note: Stores the last leaf record left in the load_args->out_recdes
 * area to the last leaf page (or to the last overflow page).
 * Then it saves the last leaf page (and the last overflow page,
 * if there is one) to the disk.
 */
static int
btree_save_last_leafrec (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args)
{
  INT16 slotid;
  int sp_success;
  int cur_maxspace;
  int ret = NO_ERROR;
  BTREE_NODE_HEADER *header = NULL;

  if (load_args->overflowing == true)
    {
      /* Store the record in current overflow page */
      sp_success = spage_insert (thread_p, load_args->ovf.pgptr, &load_args->ovf_recdes, &slotid);
      if (sp_success != SP_SUCCESS)
	{
	  goto exit_on_error;
	}

      assert (slotid > 0);

      /* Save the current overflow page */
      ret = btree_log_page (thread_p, &load_args->btid->sys_btid->vfid, load_args->ovf.pgptr, load_args->no_redo,
			    load_args->provider != NULL);
      load_args->ovf.pgptr = NULL;
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  /* Insert leaf record too */
  cur_maxspace = spage_max_space_for_new_record (thread_p, load_args->leaf.pgptr);
  if (((cur_maxspace - load_args->leaf_nleaf_recdes.length) < LOAD_FIXED_EMPTY_FOR_LEAF)
      && (spage_number_of_records (load_args->leaf.pgptr) > 1))
    {
      /* New record does not fit into the current leaf page (within the threshold value); so allocate a new leaf page
       * and dump the current leaf page. */
      if (btree_proceed_leaf (thread_p, load_args) == NULL)
	{
	  goto exit_on_error;
	}
    }

  /* Insert the record to the current leaf page */
  sp_success = spage_insert (thread_p, load_args->leaf.pgptr, &load_args->leaf_nleaf_recdes, &slotid);
  if (sp_success != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  assert (slotid > 0);

  /* Update the node header information for this record */
  if (load_args->cur_key_len >= BTREE_MAX_KEYLEN_INPAGE)
    {
      if (load_args->leaf.hdr.max_key_len < DISK_VPID_SIZE)
	{
	  load_args->leaf.hdr.max_key_len = DISK_VPID_SIZE;
	}
    }
  else
    {
      if (load_args->leaf.hdr.max_key_len < load_args->cur_key_len)
	{
	  load_args->leaf.hdr.max_key_len = load_args->cur_key_len;
	}
    }
  load_args->report_local_max_key_len =
    MAX (load_args->report_local_max_key_len, (int) load_args->leaf.hdr.max_key_len);

  /* Update the leaf header of the current leaf page */
  header = btree_get_node_header (thread_p, load_args->leaf.pgptr);
  if (header == NULL)
    {
      goto exit_on_error;
    }

  *header = load_args->leaf.hdr;

  /* Save the current leaf page */
  ret = btree_log_page (thread_p, &load_args->btid->sys_btid->vfid, load_args->leaf.pgptr, load_args->no_redo,
			load_args->provider != NULL);
  load_args->leaf.pgptr = NULL;
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  return ret;

exit_on_error:

  pgbuf_unfix_and_init_after_check (thread_p, load_args->leaf.pgptr);
  pgbuf_unfix_and_init_after_check (thread_p, load_args->ovf.pgptr);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_connect_page () - Connect child page to the new parent page
 *   return: PAGE_PTR (pointer to the parent page if finished successfully,
 *                     or NULL in case of an error)
 *   key(in): Biggest key value (or, separator value in case of string
 *            keys) of the child page to be connected.
 *   max_key_len(in): size of the biggest key in this leaf page
 *   pageid(in): identifier of this leaf page
 *   load_args(in):
 *
 * Note: This function connects a page to its parent page. In the
 * parent level a new record is prepared with the given & pageid
 * parameters and inserted into current non-leaf page being
 * filled up. If the record does not fit into this page within
 * the given threshold boundaries (LOAD_FIXED_EMPTY_FOR_NONLEAF bytes of each
 * page are reserved for future insertions) then this page is
 * flushed to disk and a new page is allocated to become the
 * current non-leaf page.
 */

static PAGE_PTR
btree_connect_page (THREAD_ENTRY * thread_p, DB_VALUE * key, int max_key_len, VPID * pageid, LOAD_ARGS * load_args,
		    int node_level)
{
  NON_LEAF_REC nleaf_rec;
  INT16 slotid;
  int sp_success;
  int cur_maxspace;
  int key_len;
  int key_type = BTREE_NORMAL_KEY;
  BTREE_NODE_HEADER *header = NULL;

  /* form the leaf record (create the header & insert the key) */
  cur_maxspace = spage_max_space_for_new_record (thread_p, load_args->nleaf.pgptr);

  nleaf_rec.pnt = *pageid;
  key_len = btree_get_disk_size_of_key (key);
  if (key_len < BTREE_MAX_KEYLEN_INPAGE)
    {
      nleaf_rec.key_len = key_len;
      key_type = BTREE_NORMAL_KEY;
    }
  else
    {
      nleaf_rec.key_len = -1;
      key_type = BTREE_OVERFLOW_KEY;

      if (VFID_ISNULL (&load_args->btid->ovfid))
	{
	  if (btree_create_overflow_key_file (thread_p, load_args->btid) != NO_ERROR)
	    {
	      return NULL;
	    }
	}
    }

  if (bt_load_write_record (thread_p, load_args, &nleaf_rec, key, BTREE_NON_LEAF_NODE, key_type, key_len,
			    NULL, NULL, NULL, &load_args->leaf_nleaf_recdes) != NO_ERROR)
    {
      return NULL;
    }

  if ((cur_maxspace - load_args->leaf_nleaf_recdes.length) < LOAD_FIXED_EMPTY_FOR_NONLEAF)
    {

      /* New record does not fit into the current non-leaf page (within the threshold value); so allocate a new
       * non-leaf page and dump the current non-leaf page. */

      /* Update the non-leaf page header */
      header = btree_get_node_header (thread_p, load_args->nleaf.pgptr);
      if (header == NULL)
	{
	  return NULL;
	}

      *header = load_args->nleaf.hdr;

      /* Flush the current non-leaf page */
      if (btree_log_page (thread_p, &load_args->btid->sys_btid->vfid, load_args->nleaf.pgptr, load_args->no_redo,
			  load_args->provider != NULL) != NO_ERROR)
	{
	  load_args->nleaf.pgptr = NULL;
	  return NULL;
	}
      load_args->nleaf.pgptr = NULL;

      /* Insert the pageid to the linked list */
      if (list_add (&load_args->push_list, &load_args->nleaf.vpid) != NO_ERROR)
	{
	  return NULL;
	}

      /* get a new non-leaf page */
      if ((*load_args->new_page_fn) (thread_p, load_args, &load_args->nleaf.hdr, node_level,
				     &load_args->nleaf.vpid, &load_args->nleaf.pgptr) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return NULL;
	}
      if (load_args->nleaf.pgptr == NULL)
	{
	  assert_release (false);
	  return NULL;
	}
    }				/* get a new leaf */

  /* Insert the record to the current leaf page */
  sp_success = spage_insert (thread_p, load_args->nleaf.pgptr, &load_args->leaf_nleaf_recdes, &slotid);
  if (sp_success != SP_SUCCESS)
    {
      return NULL;
    }

  assert (slotid > 0);

  /* Update the node header information for this record */
  if (load_args->nleaf.hdr.max_key_len < max_key_len)
    {
      load_args->nleaf.hdr.max_key_len = max_key_len;
    }

  return load_args->nleaf.pgptr;
}

/*
 * btree_build_nleafs () -
 *   return: NO_ERROR
 *   load_args(in): Collection of information on how to build B+tree.
 *   n_nulls(in):
 *   n_oids(in):
 *   n_keys(in):
 *
 * Note: This function builds the non-leaf level nodes of the B+Tree
 * index in three phases. In the first phase, the first non-leaf
 * level nodes of the tree are constructed. Then, the upper
 * levels are built on top of this level, and finally, the last
 * non-leaf page (the single page of the top level) is updated
 * to become the root page.
 */
static int
btree_build_nleafs (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, int n_nulls, int n_oids, int n_keys)
{
  RECDES temp_recdes;		/* Temporary record descriptor; */
  char *temp_data = NULL;
  VPID next_vpid;
  PAGE_PTR next_pageptr = NULL;

  /* Variables used in the second phase to go over lower level non-leaf pages */
  VPID cur_nleafpgid;
  PAGE_PTR cur_nleafpgptr = NULL;

  BTREE_NODE *temp;
  BTREE_ROOT_HEADER root_header_info, *root_header = NULL;
  BTREE_NODE_HEADER *header = NULL;
  int key_cnt;
  int max_key_len, new_max;
  LEAF_REC leaf_pnt;
  NON_LEAF_REC nleaf_pnt;
  /* Variables for keeping keys */
  DB_VALUE prefix_key;		/* Prefix key; prefix of last & first keys; used only if type is one of the string
				 * types */
  int last_key_offset, first_key_offset;
  bool clear_last_key = false, clear_first_key = false;

  int ret = NO_ERROR;
  int node_level = 2;		/* leaf level = 1, lowest non-leaf level = 2 */
  RECDES rec;
  char rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  DB_VALUE last_key;		/* Last key of the current page */
  DB_VALUE first_key;		/* First key of the next page; used only if key_type is one of the string types */

  root_header = &root_header_info;

  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (rec_buf, BTREE_MAX_ALIGN);

  btree_init_temp_key_value (&clear_last_key, &last_key);
  btree_init_temp_key_value (&clear_first_key, &first_key);
  db_make_null (&prefix_key);

  temp_data = (char *) os_malloc (DB_PAGESIZE);
  if (temp_data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_PAGESIZE);
      ret = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }

  /*****************************************************
      PHASE I : Build the first non_leaf level nodes
   ****************************************************/

  assert (node_level == 2);

  /* Initialize the first non-leaf page */
  ret =
    (*load_args->new_page_fn) (thread_p, load_args, &load_args->nleaf.hdr, node_level, &load_args->nleaf.vpid,
			       &load_args->nleaf.pgptr);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }
  if (load_args->nleaf.pgptr == NULL)
    {
      assert_release (false);
      ret = ER_FAILED;
      goto end;
    }

  load_args->push_list = NULL;
  load_args->pop_list = NULL;

  db_make_null (&last_key);

  /* While there are some leaf pages do */
  load_args->leaf.vpid = load_args->vpid_first_leaf;
  while (!VPID_ISNULL (&(load_args->leaf.vpid)))
    {
      load_args->leaf.pgptr =
	pgbuf_fix (thread_p, &load_args->leaf.vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (load_args->leaf.pgptr == NULL)
	{
	  ASSERT_ERROR_AND_SET (ret);
	  goto end;
	}

#if !defined (NDEBUG)
      (void) pgbuf_check_page_ptype (thread_p, load_args->leaf.pgptr, PAGE_BTREE);
#endif /* !NDEBUG */

      key_cnt = btree_node_number_of_keys (thread_p, load_args->leaf.pgptr);
      assert (key_cnt > 0);

      /* obtain the header information for the leaf page */
      header = btree_get_node_header (thread_p, load_args->leaf.pgptr);
      if (header == NULL)
	{
	  assert_release (false);
	  ret = ER_FAILED;
	  goto end;
	}

      /* get the maximum key length on this leaf page */
      max_key_len = header->max_key_len;
      next_vpid = header->next_vpid;

      /* set level 2 to first non-leaf page */
      load_args->nleaf.hdr.node_level = node_level;

      /* Learn the first key of the leaf page */
      if (spage_get_record (thread_p, load_args->leaf.pgptr, 1, &temp_recdes, PEEK) != S_SUCCESS)

	{
	  assert_release (false);
	  ret = ER_FAILED;
	  goto end;
	}

      ret =
	btree_read_record (thread_p, load_args->btid, load_args->leaf.pgptr, &temp_recdes, &first_key, &leaf_pnt,
			   BTREE_LEAF_NODE, &clear_first_key, &first_key_offset, PEEK_KEY_VALUE, NULL);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto end;
	}

      if (pr_is_prefix_key_type (TP_DOMAIN_TYPE (load_args->btid->key_type)))
	{
	  /*
	   * Key type is string or midxkey.
	   * Should insert the prefix key to the parent level
	   */
	  if (DB_IS_NULL (&last_key))
	    {
	      /* is the first leaf When the types of leaf node are char, bit, the type that is saved on non-leaf
	       * node is different. non-leaf spec (char -> varchar, bit -> varbit) hence it should
	       * be configured by using setval of nonleaf_key_type. */
	      ret = load_args->btid->nonleaf_key_type->type->setval (&prefix_key, &first_key, true);
	      if (ret != NO_ERROR)
		{
		  assert (!"setval error");
		  goto end;
		}
	    }
	  else
	    {
	      /* Insert the prefix key to the parent level */
	      ret = btree_get_prefix_separator (&last_key, &first_key, &prefix_key, load_args->btid->key_type);
	      if (ret != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto end;
		}
	    }

	  /*
	   * We may need to update the max_key length if the mid key is
	   * larger than the max key length.  This will only happen when
	   * the varying key length is larger than the fixed key length
	   * in pathological cases like char(4)
	   */
	  new_max = btree_get_disk_size_of_key (&prefix_key);
	  new_max = BTREE_GET_KEY_LEN_IN_PAGE (new_max);
	  max_key_len = MAX (new_max, max_key_len);

	  assert (node_level == 2);
	  if (btree_connect_page (thread_p, &prefix_key, max_key_len, &load_args->leaf.vpid, load_args, node_level) ==
	      NULL)
	    {
	      ASSERT_ERROR_AND_SET (ret);
	      pr_clear_value (&prefix_key);
	      goto end;
	    }

	  /* We always need to clear the prefix key. It is always a copy. */
	  pr_clear_value (&prefix_key);

	  btree_clear_key_value (&clear_last_key, &last_key);

	  /* Learn the last key of the leaf page */
	  assert (key_cnt > 0);
	  if (spage_get_record (thread_p, load_args->leaf.pgptr, key_cnt, &temp_recdes, PEEK) != S_SUCCESS)
	    {
	      assert_release (false);
	      ret = ER_FAILED;
	      goto end;
	    }

	  ret =
	    btree_read_record (thread_p, load_args->btid, load_args->leaf.pgptr, &temp_recdes, &last_key, &leaf_pnt,
			       BTREE_LEAF_NODE, &clear_last_key, &last_key_offset, PEEK_KEY_VALUE, NULL);
	  if (ret != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto end;
	    }
	}
      else
	{
	  max_key_len = BTREE_GET_KEY_LEN_IN_PAGE (max_key_len);

	  /* Insert this key to the parent level */
	  assert (node_level == 2);
	  if (btree_connect_page (thread_p, &first_key, max_key_len, &load_args->leaf.vpid, load_args, node_level) ==
	      NULL)
	    {
	      ASSERT_ERROR_AND_SET (ret);
	      goto end;
	    }
	}

      btree_clear_key_value (&clear_first_key, &first_key);

      /* Proceed the current leaf page pointer to the next leaf page */
      pgbuf_unfix_and_init (thread_p, load_args->leaf.pgptr);
      load_args->leaf.vpid = next_vpid;
      load_args->leaf.pgptr = next_pageptr;
      next_pageptr = NULL;

    }				/* while there are some more leaf pages */


  /* Update the non-leaf page header */
  header = btree_get_node_header (thread_p, load_args->nleaf.pgptr);
  if (header == NULL)
    {
      assert_release (false);
      ret = ER_FAILED;
      goto end;
    }

  *header = load_args->nleaf.hdr;

  /* Flush the last non-leaf page */
  ret = btree_log_page (thread_p, &load_args->btid->sys_btid->vfid, load_args->nleaf.pgptr, load_args->no_redo,
			load_args->provider != NULL);
  load_args->nleaf.pgptr = NULL;
  if (ret != NO_ERROR)
    {
      goto end;
    }

  /* Insert the pageid to the linked list */
  ret = list_add (&load_args->push_list, &load_args->nleaf.vpid);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  btree_clear_key_value (&clear_last_key, &last_key);

  /*****************************************************
      PHASE II: Build the upper levels of tree
   ****************************************************/

  assert (node_level == 2);

  /* Switch the push and pop lists */
  load_args->pop_list = load_args->push_list;
  load_args->push_list = NULL;

  while (list_length (load_args->pop_list) > 1)
    {
      node_level++;

      /* while there are more than one page at the previous level do construct the next level */

      /* Initialize the first non-leaf page of current level */
      ret =
	(*load_args->new_page_fn) (thread_p, load_args, &load_args->nleaf.hdr, node_level,
				   &load_args->nleaf.vpid, &load_args->nleaf.pgptr);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto end;
	}
      if (load_args->nleaf.pgptr == NULL)
	{
	  assert_release (false);
	  ret = ER_FAILED;
	  goto end;
	}

      do
	{			/* while pop_list is not empty */
	  /* Get current pageid from the poplist */
	  cur_nleafpgid = load_args->pop_list->pageid;

	  /* Fetch the current page */
	  cur_nleafpgptr = pgbuf_fix (thread_p, &cur_nleafpgid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (cur_nleafpgptr == NULL)
	    {
	      ASSERT_ERROR_AND_SET (ret);
	      goto end;
	    }

#if !defined (NDEBUG)
	  (void) pgbuf_check_page_ptype (thread_p, cur_nleafpgptr, PAGE_BTREE);
#endif /* !NDEBUG */

	  /* obtain the header information for the current non-leaf page */
	  header = btree_get_node_header (thread_p, cur_nleafpgptr);
	  if (header == NULL)
	    {
	      assert_release (false);
	      ret = ER_FAILED;
	      goto end;
	    }

	  /* get the maximum key length on this leaf page */
	  max_key_len = header->max_key_len;

	  /* Learn the first key of the current page */
	  /* Notice that since this is a non-leaf node */
	  if (spage_get_record (thread_p, cur_nleafpgptr, 1, &temp_recdes, PEEK) != S_SUCCESS)
	    {
	      assert_release (false);
	      ret = ER_FAILED;
	      goto end;
	    }

	  ret =
	    btree_read_record (thread_p, load_args->btid, cur_nleafpgptr, &temp_recdes, &first_key, &nleaf_pnt,
			       BTREE_NON_LEAF_NODE, &clear_first_key, &first_key_offset, PEEK_KEY_VALUE, NULL);
	  if (ret != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto end;
	    }

	  max_key_len = BTREE_GET_KEY_LEN_IN_PAGE (max_key_len);

	  /* set level to non-leaf page nleaf page could be changed in btree_connect_page */

	  assert (node_level > 2);

	  load_args->nleaf.hdr.node_level = node_level;

	  /* Insert this key to the parent level */
	  if (btree_connect_page (thread_p, &first_key, max_key_len, &cur_nleafpgid, load_args, node_level) == NULL)
	    {
	      ASSERT_ERROR_AND_SET (ret);
	      goto end;
	    }

	  pgbuf_unfix_and_init (thread_p, cur_nleafpgptr);

	  /* Remove this pageid from the pop list */
	  list_remove_first (&load_args->pop_list);

	  btree_clear_key_value (&clear_first_key, &first_key);
	}
      while (list_length (load_args->pop_list) > 0);

      /* FLUSH LAST NON-LEAF PAGE */

      /* Update the non-leaf page header */
      header = btree_get_node_header (thread_p, load_args->nleaf.pgptr);
      if (header == NULL)
	{
	  assert_release (false);
	  ret = ER_FAILED;
	  goto end;
	}

      *header = load_args->nleaf.hdr;

      /* Flush the last non-leaf page */
      ret = btree_log_page (thread_p, &load_args->btid->sys_btid->vfid, load_args->nleaf.pgptr, load_args->no_redo,
			    load_args->provider != NULL);
      load_args->nleaf.pgptr = NULL;
      if (ret != NO_ERROR)
	{
	  goto end;
	}

      /* Insert the pageid to the linked list */
      ret = list_add (&load_args->push_list, &load_args->nleaf.vpid);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto end;
	}

      /* Switch push and pop lists */
      temp = load_args->pop_list;
      load_args->pop_list = load_args->push_list;
      load_args->push_list = temp;
    }

  /* Deallocate the last node (only one exists) */
  list_remove_first (&load_args->pop_list);

  /******************************************
      PHASE III: Update the root page
   *****************************************/

  /*
   * Reconcile unused provider pages before publishing the root.  file_alloc_multiple/file_dealloc may touch the
   * sticky root while the index file is still being built; the final root copy must be the last page mutation.
   */
  if (load_args->provider != NULL)
    {
      ret = bt_load_provider_reconcile (thread_p, load_args->provider, load_args->report_overflow_key_count == 0);
      if (ret != NO_ERROR)
	{
	  goto end;
	}
    }
  if (load_args->provider != NULL && load_args->report_overflow_key_count == 0
      && !VFID_ISNULL (&load_args->btid->ovfid))
    {
      /*
       * Parallel builds pre-create the overflow key file (>= 2 shards) even when no shard ends up
       * storing an overflow key.  Clear the reference and schedule the file for destruction here, before the
       * root header below is built, so the WAL-published root copy is the only mutation to the sticky root page
       * and never carries a dangling VFID through media recovery.
       */
      file_postpone_destroy (thread_p, &load_args->btid->ovfid);
      VFID_SET_NULL (&load_args->btid->ovfid);
    }

  /* Retrieve the last non-leaf page (the current one); guaranteed to exist */
  /* Fetch the current page */
  load_args->nleaf.pgptr =
    pgbuf_fix (thread_p, &load_args->nleaf.vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (load_args->nleaf.pgptr == NULL)
    {
      ASSERT_ERROR_AND_SET (ret);
      goto end;
    }
#if !defined (NDEBUG)
  pgbuf_check_page_ptype (thread_p, load_args->nleaf.pgptr, PAGE_BTREE);
#endif /* !NDEBUG */

  /* Prepare the root header by using the last leaf node header */
  root_header->node.max_key_len = load_args->nleaf.hdr.max_key_len;
  root_header->node.node_level = node_level;
  VPID_SET_NULL (&(root_header->node.next_vpid));
  VPID_SET_NULL (&(root_header->node.prev_vpid));
  root_header->node.split_info.pivot = 0.0f;
  root_header->node.split_info.index = 0;

  if (load_args->btid->unique_pk)
    {
      root_header->num_nulls = n_nulls;
      root_header->num_oids = n_oids;
      root_header->num_keys = n_keys;
      root_header->unique_pk = load_args->btid->unique_pk;

      assert (BTREE_IS_UNIQUE (root_header->unique_pk));
      assert (BTREE_IS_PRIMARY_KEY (root_header->unique_pk) || !BTREE_IS_PRIMARY_KEY (root_header->unique_pk));
    }
  else
    {
      root_header->num_nulls = -1;
      root_header->num_oids = -1;
      root_header->num_keys = -1;
      root_header->unique_pk = 0;
    }

  COPY_OID (&(root_header->topclass_oid), &load_args->btid->topclass_oid);

  root_header->ovfid = load_args->btid->ovfid;	/* structure copy */

  root_header->_32.rev_level = BTREE_CURRENT_REV_LEVEL;
  SET_DECOMPRESS_IDX_HEADER (root_header, load_args->btid->deduplicate_key_idx);

#if defined (SERVER_MODE)
  root_header->creator_mvccid = logtb_get_current_mvccid (thread_p);
#else	/* !SERVER_MODE */		   /* SA_MODE */
  root_header->creator_mvccid = MVCCID_NULL;
#endif /* SA_MODE */

  /* change node header as root header */
  ret = btree_pack_root_header (&rec, root_header, load_args->btid->key_type);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (spage_update (thread_p, load_args->nleaf.pgptr, HEADER, &rec) != SP_SUCCESS)
    {
      assert_release (false);
      ret = ER_FAILED;
      goto end;
    }

  /* Publish on the file's authoritative sticky root page. */
  if (load_args->provider != NULL)
    {
      cur_nleafpgid = load_args->provider->root_vpid;
    }
  else
    {
      btree_get_root_vpid_from_btid (thread_p, load_args->btid->sys_btid, &cur_nleafpgid);
    }
  next_pageptr = pgbuf_fix (thread_p, &cur_nleafpgid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (next_pageptr == NULL)
    {
      ASSERT_ERROR_AND_SET (ret);
      goto end;
    }
  assert (pgbuf_get_page_ptype (thread_p, next_pageptr) == PAGE_BTREE);

  memcpy (next_pageptr, load_args->nleaf.pgptr, DB_PAGESIZE);
  pgbuf_unfix_and_init (thread_p, load_args->nleaf.pgptr);

  load_args->nleaf.pgptr = next_pageptr;
  next_pageptr = NULL;
  load_args->nleaf.vpid = cur_nleafpgid;

  /*
   * The sticky root predates the no-redo provider allocations.  Publish it through WAL so commit/recovery cannot
   * expose its original empty slotted-page image.
   */
  ret = btree_log_page (thread_p, &load_args->btid->sys_btid->vfid, load_args->nleaf.pgptr, false, false);
  load_args->nleaf.pgptr = NULL;
  if (ret != NO_ERROR)
    {
      goto end;
    }

  assert (ret == NO_ERROR);

end:

  /* cleanup */
  pgbuf_unfix_and_init_after_check (thread_p, next_pageptr);
  pgbuf_unfix_and_init_after_check (thread_p, cur_nleafpgptr);
  pgbuf_unfix_and_init_after_check (thread_p, load_args->leaf.pgptr);
  pgbuf_unfix_and_init_after_check (thread_p, load_args->nleaf.pgptr);

  if (temp_data)
    {
      os_free_and_init (temp_data);
    }

  btree_clear_key_value (&clear_last_key, &last_key);
  btree_clear_key_value (&clear_first_key, &first_key);

  return ret;
}

/*
 * btree_log_page () - Save the contents of the buffer
 *   return: error code
 *   vfid(in):
 *   page_ptr(in): pointer to the page buffer to be saved; consumed and
 *                 unfixed on both success and failure
 *   no_redo(in): when true (bulk build), skip the full-page content redo
 *   flush_after_unfix(in): when true (parallel bulk builds only), submit the
 *                          no-redo page to the ordinary flush path right after
 *                          the unfix (best effort; never fails the build)
 *
 * Note: This function logs the contents (unless no_redo) and frees
 * the page after setting the dirty bit.
 */
static int
btree_log_page (THREAD_ENTRY * thread_p, VFID * vfid, PAGE_PTR page_ptr, bool no_redo, bool flush_after_unfix)
{
  bool write_through = false;
  VPID vpid;

  if (!no_redo)
    {
      LOG_DATA_ADDR addr;

      addr.vfid = vfid;
      addr.pgptr = page_ptr;
      addr.offset = -1;
      log_append_redo_data (thread_p, RVBT_COPYPAGE, &addr, DB_PAGESIZE, page_ptr);
    }
  /*
   * no_redo == true: bulk-build pages skip ONLY the full-page content redo (RVBT_COPYPAGE).
   * The page reaches disk through the ordinary flush path (background flusher, eviction,
   * checkpoint, the dump-time write-through below, and the pre-publication durability
   * barrier in xbtree_load_index), including DWB staging when enabled.  Content changes are
   * never logged, so oldest_unflush_lsa stays NULL after the first flush and the WAL-force
   * step no-ops; equal-LSA restagings are the DWB's designed "flushing to disk without
   * logging" case (double_write_buffer.cpp slot-hash dedup).  Durability before publication
   * comes from the explicit barrier in xbtree_load_index, not from this function.
   */
  if (no_redo && flush_after_unfix && prm_get_bool_value (PRM_ID_BULK_BUILD_WORKER_FLUSH))
    {
      /* capture the page identity before the unfix below consumes page_ptr. */
      pgbuf_get_vpid (page_ptr, &vpid);
      write_through = true;
    }
  pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);
  pgbuf_unfix_and_init (thread_p, page_ptr);

  if (write_through)
    {
      /*
       * Write-through: submit the finished bulk page to the ordinary flush path now (write only; the
       * single per-build DWB drain + fsync stays in the pre-publication barrier), restoring
       * the parallel write overlap the defer-to-barrier scheme lost.  Best effort: on any
       * failure the page simply stays dirty and the barrier flushes it later, so the error
       * is logged and swallowed without touching the caller's er state.  No latch is held
       * on this page here, so the probe-flush cannot self-deadlock; latches the caller may
       * hold on OTHER build pages are irrelevant to the flush machinery (it takes only the
       * hash and bcb mutexes, never a page latch).
       */
      er_stack_push ();
      if (pgbuf_flush_page_if_exists_and_dirty (thread_p, &vpid, NULL) != NO_ERROR)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "DEBUG_BTREE: bulk write-through flush failed for vpid(%d, %d) (error = %d); "
			 "the page stays dirty for the pre-publication barrier.", vpid.volid, vpid.pageid, er_errid ());
	}
      er_stack_pop ();
    }
  return NO_ERROR;
}

static int
bt_load_allocate_span_start (THREAD_ENTRY * thread_p, const VFID * vfid, bool is_ovf, int npages, VPID ** vpids_out)
{
  VPID *vpids;
  PAGE_TYPE ptype = PAGE_OVERFLOW;
  int error;

  if (npages <= 0 || (size_t) npages > SIZE_MAX / sizeof (VPID))
    {
      return ER_FAILED;
    }
  vpids = (VPID *) malloc ((size_t) npages * sizeof (VPID));
  if (vpids == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) npages * sizeof (VPID));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  log_sysop_start (thread_p);
  error = file_alloc_multiple (thread_p, vfid, is_ovf ? file_init_page_type : btree_initialize_new_page,
			       is_ovf ? &ptype : NULL, npages, vpids);
  if (error != NO_ERROR)
    {
      log_sysop_abort (thread_p);
      free_and_init (vpids);
      return error;
    }
  *vpids_out = vpids;
  return NO_ERROR;
}

static int
bt_load_allocate_span (THREAD_ENTRY * thread_p, const VFID * vfid, bool is_ovf, int npages, VPID ** vpids_out)
{
  int error = bt_load_allocate_span_start (thread_p, vfid, is_ovf, npages, vpids_out);
  if (error == NO_ERROR)
    {
      log_sysop_attach_to_outer (thread_p);
    }
  return error;
}

static BT_LOAD_ALLOCATION_LEDGER *
bt_load_make_ledger_entry (const VFID * vfid, const VPID * vpids, int n, BT_LOAD_ALLOCATION_ORIGIN origin)
{
  BT_LOAD_ALLOCATION_LEDGER *entry;

  if (n <= 0 || (size_t) n > SIZE_MAX / sizeof (VPID))
    {
      return NULL;
    }
  entry = (BT_LOAD_ALLOCATION_LEDGER *) malloc (sizeof (*entry));
  if (entry == NULL)
    {
      return NULL;
    }
  entry->vpids = (VPID *) malloc ((size_t) n * sizeof (*entry->vpids));
  if (entry->vpids == NULL)
    {
      free_and_init (entry);
      return NULL;
    }
  entry->vfid = *vfid;
  memcpy (entry->vpids, vpids, (size_t) n * sizeof (*entry->vpids));
  entry->n = n;
  entry->origin = origin;
  entry->next = NULL;
  return entry;
}

static void
bt_load_append_ledger_entry (BT_LOAD_PROVIDER * provider, BT_LOAD_ALLOCATION_LEDGER * entry)
{
  entry->next = provider->ledger;
  provider->ledger = entry;
  provider->allocated_pages += entry->n;
}

static void
bt_load_free_ledger_entry (BT_LOAD_ALLOCATION_LEDGER * entry)
{
  if (entry != NULL)
    {
      free_and_init (entry->vpids);
      free_and_init (entry);
    }
}

static BT_LOAD_SPAN *
bt_load_make_span (VPID * vpids, int n, bool owns_vpids, bool is_ovf)
{
  BT_LOAD_SPAN *span = (BT_LOAD_SPAN *) malloc (sizeof (*span));
  if (span != NULL)
    {
      span->vpids = vpids;
      span->n = n;
      span->used = 0;
      span->owns_vpids = owns_vpids;
      span->is_ovf = is_ovf;
      span->next = NULL;
    }
  return span;
}

static int
bt_load_claim_pool_span (BT_LOAD_VPID_POOL * pool, bool is_ovf, BT_LOAD_SPAN ** out)
{
  int current = pool->cursor.load (std::memory_order_relaxed);
  while (current < pool->n_published)
    {
      int take = MIN (64, pool->n_published - current);
      if (pool->cursor.compare_exchange_weak (current, current + take, std::memory_order_acq_rel,
					      std::memory_order_relaxed))
	{
	  *out = bt_load_make_span (&pool->vpids[current], take, false, is_ovf);
	  return *out == NULL ? ER_OUT_OF_VIRTUAL_MEMORY : NO_ERROR;
	}
    }
  return ER_FAILED;
}

static void
bt_load_add_span (BT_LOAD_SPAN ** list, BT_LOAD_SPAN * span)
{
  span->next = *list;
  *list = span;
}

static int
bt_load_provider_claim_page (BT_LOAD_PROVIDER * provider, int worker_idx, bool is_ovf, VPID * vpid)
{
  BT_LOAD_WORKER_SLOT *slot = &provider->slots[worker_idx];
  BT_LOAD_SPAN **list = is_ovf ? &slot->ovf_spans : &slot->main_spans;
  BT_LOAD_VPID_POOL *pool = is_ovf ? &provider->ovf_pool : &provider->main_pool;
  BT_LOAD_SPAN *span = *list;
  int error;

  for (;;)
    {
      if (span != NULL && span->used < span->n)
	{
	  *vpid = span->vpids[span->used++];
	  slot->consumed_pages++;
	  if (is_ovf)
	    {
	      slot->consumed_ovf_pages++;
	    }
	  if (span->n - span->used <= span->n / 4 && pool->cursor.load (std::memory_order_relaxed) >= pool->n_published)
	    {
	      pthread_mutex_lock (&provider->mtx);
	      if (slot->state == BT_LOAD_SLOT_IDLE)
		{
		  slot->state = is_ovf ? BT_LOAD_SLOT_NEED_OVF : BT_LOAD_SLOT_NEED_BTREE;
		  slot->req_npages = 64;
		  pthread_cond_signal (&provider->cond_main);
		}
	      pthread_mutex_unlock (&provider->mtx);
	    }
	  return NO_ERROR;
	}
      error = bt_load_claim_pool_span (pool, is_ovf, &span);
      if (error == NO_ERROR)
	{
	  bt_load_add_span (list, span);
	  continue;
	}
      if (error != ER_FAILED)
	{
	  return error;
	}

      pthread_mutex_lock (&provider->mtx);
      if (slot->state == BT_LOAD_SLOT_READY)
	{
	  span = slot->ready_span;
	  slot->ready_span = NULL;
	  slot->state = BT_LOAD_SLOT_IDLE;
	  bt_load_add_span (span->is_ovf ? &slot->ovf_spans : &slot->main_spans, span);
	  span = *list;
	  pthread_mutex_unlock (&provider->mtx);
	  continue;
	}
      if (slot->state == BT_LOAD_SLOT_IDLE)
	{
	  slot->state = is_ovf ? BT_LOAD_SLOT_NEED_OVF : BT_LOAD_SLOT_NEED_BTREE;
	  slot->req_npages = 64;
	  pthread_cond_signal (&provider->cond_main);
	}
      while (slot->state != BT_LOAD_SLOT_READY && provider->first_error == NO_ERROR)
	{
	  pthread_cond_wait (&provider->cond_worker[worker_idx], &provider->mtx);
	}
      if (provider->first_error != NO_ERROR)
	{
	  error = provider->first_error;
	  pthread_mutex_unlock (&provider->mtx);
	  return error;
	}
      span = slot->ready_span;
      slot->ready_span = NULL;
      slot->state = BT_LOAD_SLOT_IDLE;
      bt_load_add_span (span->is_ovf ? &slot->ovf_spans : &slot->main_spans, span);
      span = *list;
      pthread_mutex_unlock (&provider->mtx);
    }
}

int
bt_load_provider_open (THREAD_ENTRY * thread_p, BT_LOAD_PROVIDER ** out, const BTID * btid, int n_workers,
		       int est_main_pages, int est_ovf_pages, bool need_ovf_file)
{
  BT_LOAD_PROVIDER *provider;
  int error;
  VPID root_vpid;

  if (out == NULL || btid == NULL || n_workers < 2)
    {
      return ER_FAILED;
    }

  error = file_get_sticky_first_page (thread_p, &btid->vfid, &root_vpid);
  if (error != NO_ERROR)
    {
      return error;
    }
  provider = new BT_LOAD_PROVIDER ();
  provider->main_pool.vpids = NULL;
  /*
   * Publish only a small bootstrap pool (one 64-page span per worker) and leave the rest to the service loop's
   * on-demand 64-page refills.  est_main_pages is derived from the sorted runs' page count, which overshoots the
   * real index size several times over for duplicate-heavy keys (the runs store every (key, OID) record, the
   * index dedups keys); publishing the whole estimate up front would allocate and format that many pages in one
   * burst before any worker starts, flooding the page pool with formatted-empty pages, flushing them to disk as
   * waste I/O, starving worker fixes on victim waits, and leaving most of the pages to be deallocated again by
   * the reconcile pass.  The claim protocol already pre-requests refills when a span is 75% consumed, so a small
   * bootstrap keeps workers fed without the burst.
   */
  provider->main_pool.n_published = MIN (MAX (est_main_pages, 64), n_workers * 64);
  provider->main_pool.cursor.store (0, std::memory_order_relaxed);
  provider->ovf_pool.vpids = NULL;
  provider->ovf_pool.n_published = 0;
  provider->ovf_pool.cursor.store (0, std::memory_order_relaxed);
  provider->main_vfid = btid->vfid;
  provider->root_vpid = root_vpid;
  VFID_SET_NULL (&provider->ovf_vfid);
  provider->est_ovf_pages = need_ovf_file ? MAX (est_ovf_pages, 0) : 0;
  provider->n_workers = n_workers;
  provider->slots = (BT_LOAD_WORKER_SLOT *) calloc ((size_t) n_workers, sizeof (*provider->slots));
  provider->cond_worker = (pthread_cond_t *) malloc ((size_t) n_workers * sizeof (*provider->cond_worker));
  provider->first_error = NO_ERROR;
  provider->main_inline_spans = NULL;
  provider->main_inline_current = NULL;
  provider->ovf_inline_spans = NULL;
  provider->ovf_inline_current = NULL;
  provider->ledger = NULL;
  provider->allocated_pages = 0;
  provider->consumed_pages = 0;
  provider->consumed_ovf_pages = 0;
  provider->returned_pages = 0;
  if (provider->slots == NULL || provider->cond_worker == NULL)
    {
      if (provider->slots != NULL)
	{
	  free_and_init (provider->slots);
	}
      if (provider->cond_worker != NULL)
	{
	  free_and_init (provider->cond_worker);
	}
      delete provider;
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  pthread_mutex_init (&provider->mtx, NULL);
  pthread_cond_init (&provider->cond_main, NULL);
  for (int i = 0; i < n_workers; i++)
    {
      pthread_cond_init (&provider->cond_worker[i], NULL);
      provider->slots[i].state = BT_LOAD_SLOT_IDLE;
    }
  error = bt_load_allocate_span (thread_p, &provider->main_vfid, false, provider->main_pool.n_published,
				 &provider->main_pool.vpids);
  if (error != NO_ERROR)
    {
      bt_load_provider_close (provider);
      return error;
    }
  {
    BT_LOAD_ALLOCATION_LEDGER *entry = bt_load_make_ledger_entry (&provider->main_vfid, provider->main_pool.vpids,
								  provider->main_pool.n_published,
								  BT_LOAD_ORIGIN_POOL_INIT);
    if (entry == NULL)
      {
	bt_load_provider_close (provider);
	return ER_OUT_OF_VIRTUAL_MEMORY;
      }
    bt_load_append_ledger_entry (provider, entry);
  }
  *out = provider;
  return NO_ERROR;
}

int
bt_load_provider_service_loop (THREAD_ENTRY * thread_p, BT_LOAD_PROVIDER * provider)
{
  for (;;)
    {
      int selected = -1;
      bool all_done = true;
      bool is_ovf = false;
      int npages = 0;
      VPID *vpids = NULL;
      BT_LOAD_SPAN *published = NULL;
      BT_LOAD_ALLOCATION_LEDGER *ledger_entry = NULL;
      const VFID *vfid;
      int error;

      pthread_mutex_lock (&provider->mtx);
      for (int i = 0; i < provider->n_workers; i++)
	{
	  BT_LOAD_SLOT_STATE state = provider->slots[i].state;
	  if (state != BT_LOAD_SLOT_DONE && state != BT_LOAD_SLOT_ERROR)
	    {
	      all_done = false;
	    }
	  if (selected < 0 && (state == BT_LOAD_SLOT_NEED_BTREE || state == BT_LOAD_SLOT_NEED_OVF))
	    {
	      selected = i;
	      is_ovf = state == BT_LOAD_SLOT_NEED_OVF;
	      npages = provider->slots[i].req_npages;
	      provider->slots[i].state = BT_LOAD_SLOT_ALLOCATING;
	    }
	}
      if (all_done)
	{
	  pthread_mutex_unlock (&provider->mtx);
	  return provider->first_error;
	}
      if (selected < 0)
	{
	  pthread_cond_wait (&provider->cond_main, &provider->mtx);
	  pthread_mutex_unlock (&provider->mtx);
	  continue;
	}

      /* Recheck after collecting NEED and before opening the allocation sysop. */
      if (provider->first_error != NO_ERROR)
	{
	  if (provider->slots[selected].state == BT_LOAD_SLOT_ALLOCATING)
	    {
	      provider->slots[selected].state = BT_LOAD_SLOT_IDLE;
	    }
	  for (int i = 0; i < provider->n_workers; i++)
	    {
	      pthread_cond_broadcast (&provider->cond_worker[i]);
	    }
	  pthread_mutex_unlock (&provider->mtx);
	  continue;
	}
      pthread_mutex_unlock (&provider->mtx);

      vfid = is_ovf ? &provider->ovf_vfid : &provider->main_vfid;
      error = bt_load_allocate_span_start (thread_p, vfid, is_ovf, npages, &vpids);
      if (error == NO_ERROR)
	{
	  published = bt_load_make_span (vpids, npages, true, is_ovf);
	  ledger_entry = bt_load_make_ledger_entry (vfid, vpids, npages, BT_LOAD_ORIGIN_REFILL);
	  if (published == NULL || ledger_entry == NULL)
	    {
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      log_sysop_abort (thread_p);
	    }
	}

      if (error != NO_ERROR)
	{
	  if (published != NULL)
	    {
	      free_and_init (published);
	    }
	  bt_load_free_ledger_entry (ledger_entry);
	  free_and_init (vpids);
	  pthread_mutex_lock (&provider->mtx);
	  if (provider->first_error == NO_ERROR)
	    {
	      provider->first_error = error;
	    }
	  if (provider->slots[selected].state == BT_LOAD_SLOT_ALLOCATING)
	    {
	      provider->slots[selected].state = BT_LOAD_SLOT_IDLE;
	    }
	  for (int i = 0; i < provider->n_workers; i++)
	    {
	      pthread_cond_broadcast (&provider->cond_worker[i]);
	    }
	  pthread_mutex_unlock (&provider->mtx);
	  continue;
	}

      /* Allocation raced with workers; abort before attach when the first error is already latched. */
      pthread_mutex_lock (&provider->mtx);
      if (provider->first_error != NO_ERROR)
	{
	  if (provider->slots[selected].state == BT_LOAD_SLOT_ALLOCATING)
	    {
	      provider->slots[selected].state = BT_LOAD_SLOT_IDLE;
	    }
	  for (int i = 0; i < provider->n_workers; i++)
	    {
	      pthread_cond_broadcast (&provider->cond_worker[i]);
	    }
	  pthread_mutex_unlock (&provider->mtx);
	  log_sysop_abort (thread_p);
	  bt_load_free_ledger_entry (ledger_entry);
	  free_and_init (published);
	  free_and_init (vpids);
	  continue;
	}
      pthread_mutex_unlock (&provider->mtx);

      log_sysop_attach_to_outer (thread_p);

      /* Recheck after attach; an unpublished span remains ledgered for statement rollback accounting. */
      pthread_mutex_lock (&provider->mtx);
      bt_load_append_ledger_entry (provider, ledger_entry);
      if (provider->slots[selected].state != BT_LOAD_SLOT_ALLOCATING || provider->first_error != NO_ERROR)
	{
	  bt_load_add_span (is_ovf ? &provider->slots[selected].ovf_spans
			    : &provider->slots[selected].main_spans, published);
	}
      else
	{
	  provider->slots[selected].ready_span = published;
	  provider->slots[selected].state = BT_LOAD_SLOT_READY;
	}
      pthread_cond_signal (&provider->cond_worker[selected]);
      pthread_mutex_unlock (&provider->mtx);
    }
}

static int
bt_load_return_page (THREAD_ENTRY * thread_p, BT_LOAD_PROVIDER * provider, const VFID * vfid, FILE_TYPE file_type,
		     const VPID * vpid)
{
  if (VFID_EQ (vfid, &provider->main_vfid) && VPID_EQ (vpid, &provider->root_vpid))
    {
      /*
       * The sticky root page must never be handed back to the pool for deallocation; if it is, a double
       * allocation has aliased a pool span with the root VPID.  Surface this loudly instead of quietly treating
       * it as an already-consumed page, while still refusing to deallocate the root itself.
       */
      assert (false);
      return ER_FAILED;
    }

  int error = file_dealloc (thread_p, vfid, vpid, file_type);
  if (error == NO_ERROR)
    {
      provider->returned_pages++;
    }
  return error;
}

static int
bt_load_dealloc_span_tail (THREAD_ENTRY * thread_p, BT_LOAD_PROVIDER * provider, const VFID * vfid,
			   FILE_TYPE file_type, BT_LOAD_SPAN * span)
{
  for (int i = span->used; i < span->n; i++)
    {
      int error = bt_load_return_page (thread_p, provider, vfid, file_type, &span->vpids[i]);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  return NO_ERROR;
}

static INT64
bt_load_ledger_page_count (BT_LOAD_PROVIDER * provider, bool skip_ovf_file)
{
  INT64 count = 0;
  for (BT_LOAD_ALLOCATION_LEDGER * entry = provider->ledger; entry != NULL; entry = entry->next)
    {
      if (!skip_ovf_file || !VFID_EQ (&entry->vfid, &provider->ovf_vfid))
	{
	  count += entry->n;
	}
    }
  return count;
}

#if !defined (NDEBUG)
static int
bt_load_compare_vpid (const void *left, const void *right)
{
  const VPID *left_vpid = (const VPID *) left;
  const VPID *right_vpid = (const VPID *) right;

  if (left_vpid->volid != right_vpid->volid)
    {
      return left_vpid->volid < right_vpid->volid ? -1 : 1;
    }
  if (left_vpid->pageid != right_vpid->pageid)
    {
      return left_vpid->pageid < right_vpid->pageid ? -1 : 1;
    }
  return 0;
}

static void
bt_load_assert_ledger_has_unique_vpids (BT_LOAD_PROVIDER * provider)
{
  INT64 count = bt_load_ledger_page_count (provider, false);
  VPID *vpids;
  INT64 offset = 0;
  if (count == 0)
    {
      return;
    }

  assert (count >= 0 && (UINT64) count <= SIZE_MAX / sizeof (*vpids));
  vpids = (VPID *) malloc ((size_t) count * sizeof (*vpids));
  assert (vpids != NULL);
  if (vpids == NULL)
    {
      return;
    }
  for (BT_LOAD_ALLOCATION_LEDGER * entry = provider->ledger; entry != NULL; entry = entry->next)
    {
      assert (entry->n > 0);
      assert (entry->origin == BT_LOAD_ORIGIN_POOL_INIT || entry->origin == BT_LOAD_ORIGIN_REFILL
	      || entry->origin == BT_LOAD_ORIGIN_MAIN_INLINE);
      for (int i = 0; i < entry->n; i++)
	{
	  /* A file may allocate pages on extension volumes; only the VPID itself must be valid. */
	  assert (entry->vpids[i].volid != NULL_VOLID && entry->vpids[i].pageid != NULL_PAGEID);
	}
      memcpy (&vpids[offset], entry->vpids, (size_t) entry->n * sizeof (*vpids));
      offset += entry->n;
    }
  qsort (vpids, (size_t) count, sizeof (*vpids), bt_load_compare_vpid);
  for (INT64 i = 1; i < count; i++)
    {
      assert (!VPID_EQ (&vpids[i - 1], &vpids[i]));
    }
  free_and_init (vpids);
}
#endif

int
bt_load_provider_reconcile (THREAD_ENTRY * thread_p, BT_LOAD_PROVIDER * provider, bool skip_ovf_file)
{
  int error;
  INT64 ledger_pages;
  INT64 consumed_pages;
  INT64 consumed_ovf_pages;
  if (bt_load_ledger_page_count (provider, false) != provider->allocated_pages)
    {
      assert (false);
      return ER_FAILED;
    }
  int cursor = provider->main_pool.cursor.load (std::memory_order_relaxed);
  for (int i = cursor; i < provider->main_pool.n_published; i++)
    {
      error = bt_load_return_page (thread_p, provider, &provider->main_vfid, FILE_BTREE, &provider->main_pool.vpids[i]);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  if (!skip_ovf_file)
    {
      cursor = provider->ovf_pool.cursor.load (std::memory_order_relaxed);
      for (int i = cursor; i < provider->ovf_pool.n_published; i++)
	{
	  error = bt_load_return_page (thread_p, provider, &provider->ovf_vfid, FILE_BTREE_OVERFLOW_KEY,
				       &provider->ovf_pool.vpids[i]);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }
  for (int i = 0; i < provider->n_workers; i++)
    {
      for (BT_LOAD_SPAN * span = provider->slots[i].main_spans; span != NULL; span = span->next)
	{
	  error = bt_load_dealloc_span_tail (thread_p, provider, &provider->main_vfid, FILE_BTREE, span);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      if (!skip_ovf_file)
	{
	  for (BT_LOAD_SPAN * span = provider->slots[i].ovf_spans; span != NULL; span = span->next)
	    {
	      error = bt_load_dealloc_span_tail (thread_p, provider, &provider->ovf_vfid,
						 FILE_BTREE_OVERFLOW_KEY, span);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	    }
	}
      if (provider->slots[i].ready_span != NULL && !(skip_ovf_file && provider->slots[i].ready_span->is_ovf))
	{
	  BT_LOAD_SPAN *span = provider->slots[i].ready_span;
	  error = bt_load_dealloc_span_tail (thread_p, provider,
					     span->is_ovf ? &provider->ovf_vfid : &provider->main_vfid,
					     span->is_ovf ? FILE_BTREE_OVERFLOW_KEY : FILE_BTREE, span);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }
  for (BT_LOAD_SPAN * span = provider->main_inline_spans; span != NULL; span = span->next)
    {
      error = bt_load_dealloc_span_tail (thread_p, provider, &provider->main_vfid, FILE_BTREE, span);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  for (BT_LOAD_SPAN * span = provider->ovf_inline_spans; !skip_ovf_file && span != NULL; span = span->next)
    {
      error = bt_load_dealloc_span_tail (thread_p, provider, &provider->ovf_vfid, FILE_BTREE_OVERFLOW_KEY, span);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  consumed_pages = provider->consumed_pages;
  consumed_ovf_pages = provider->consumed_ovf_pages;
  for (int i = 0; i < provider->n_workers; i++)
    {
      if (provider->slots[i].consumed_pages < 0
	  || consumed_pages > LLONG_MAX - provider->slots[i].consumed_pages
	  || provider->slots[i].consumed_ovf_pages < 0
	  || consumed_ovf_pages > LLONG_MAX - provider->slots[i].consumed_ovf_pages)
	{
	  assert (false);
	  return ER_FAILED;
	}
      consumed_pages += provider->slots[i].consumed_pages;
      consumed_ovf_pages += provider->slots[i].consumed_ovf_pages;
    }
  if (consumed_ovf_pages > consumed_pages)
    {
      assert (false);
      return ER_FAILED;
    }
  ledger_pages = bt_load_ledger_page_count (provider, skip_ovf_file);
  consumed_pages -= skip_ovf_file ? consumed_ovf_pages : 0;
  if (ledger_pages != consumed_pages + provider->returned_pages)
    {
      assert (false);
      return ER_FAILED;
    }
  return NO_ERROR;
}

static void
bt_load_free_spans (BT_LOAD_SPAN * span)
{
  while (span != NULL)
    {
      BT_LOAD_SPAN *next = span->next;
      if (span->owns_vpids)
	{
	  free_and_init (span->vpids);
	}
      free_and_init (span);
      span = next;
    }
}

static void
bt_load_free_ledger (BT_LOAD_ALLOCATION_LEDGER * entry)
{
  while (entry != NULL)
    {
      BT_LOAD_ALLOCATION_LEDGER *next = entry->next;
      bt_load_free_ledger_entry (entry);
      entry = next;
    }
}

void
bt_load_provider_close (BT_LOAD_PROVIDER * provider)
{
  if (provider == NULL)
    {
      return;
    }
#if !defined (NDEBUG)
  bt_load_assert_ledger_has_unique_vpids (provider);
#endif
  for (int i = 0; i < provider->n_workers; i++)
    {
      bt_load_free_spans (provider->slots[i].main_spans);
      bt_load_free_spans (provider->slots[i].ovf_spans);
      bt_load_free_spans (provider->slots[i].ready_span);
      pthread_cond_destroy (&provider->cond_worker[i]);
    }
  free_and_init (provider->slots);
  free_and_init (provider->main_pool.vpids);
  free_and_init (provider->ovf_pool.vpids);
  bt_load_free_spans (provider->main_inline_spans);
  bt_load_free_spans (provider->ovf_inline_spans);
  bt_load_free_ledger (provider->ledger);
  free_and_init (provider->cond_worker);
  pthread_cond_destroy (&provider->cond_main);
  pthread_mutex_destroy (&provider->mtx);
  delete provider;
}

static int
bt_load_init_claimed_page (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, BTREE_NODE_HEADER * header, int node_level,
			   VPID * vpid_new, PAGE_PTR * page_new)
{
  int error;
  *page_new = pgbuf_fix (thread_p, vpid_new, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (*page_new == NULL)
    {
      return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
    }
  if (header != NULL)
    {
      header->node_level = node_level;
      header->max_key_len = 0;
      VPID_SET_NULL (&header->next_vpid);
      VPID_SET_NULL (&header->prev_vpid);
      header->split_info.pivot = 0.0f;
      header->split_info.index = 0;
      header->common_prefix = 0;
      error = btree_init_node_header (thread_p, &load_args->btid->sys_btid->vfid, *page_new, header, false);
    }
  else
    {
      BTREE_OVERFLOW_HEADER ovf_header;
      VPID_SET_NULL (&ovf_header.next_vpid);
      error = btree_init_overflow_header (thread_p, *page_new, &ovf_header);
    }
  if (error != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, *page_new);
    }
  return error;
}

static int
bt_load_new_page_serial (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, BTREE_NODE_HEADER * header, int node_level,
			 VPID * vpid_new, PAGE_PTR * page_new)
{
  return btree_load_new_page (thread_p, load_args->btid->sys_btid, header, node_level, vpid_new, page_new);
}

static int
bt_load_new_page_from_provider (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, BTREE_NODE_HEADER * header,
				int node_level, VPID * vpid_new, PAGE_PTR * page_new)
{
  int error = bt_load_provider_claim_page (load_args->provider, load_args->worker_idx, false, vpid_new);
  if (error != NO_ERROR)
    {
      return error;
    }
  return bt_load_init_claimed_page (thread_p, load_args, header, node_level, vpid_new, page_new);
}

static int
bt_load_new_page_main_inline (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, BTREE_NODE_HEADER * header,
			      int node_level, VPID * vpid_new, PAGE_PTR * page_new)
{
  BT_LOAD_PROVIDER *provider = load_args->provider;
  BT_LOAD_SPAN *span = provider->main_inline_current;
  int error;

  if (span == NULL || span->used == span->n)
    {
      error = bt_load_claim_pool_span (&provider->main_pool, false, &span);
      if (error != NO_ERROR)
	{
	  VPID *vpids = NULL;
	  error = bt_load_allocate_span (thread_p, &provider->main_vfid, false, 64, &vpids);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  {
	    BT_LOAD_ALLOCATION_LEDGER *entry =
	      bt_load_make_ledger_entry (&provider->main_vfid, vpids, 64, BT_LOAD_ORIGIN_MAIN_INLINE);
	    span = bt_load_make_span (vpids, 64, true, false);
	    if (span == NULL || entry == NULL)
	      {
		free_and_init (span);
		bt_load_free_ledger_entry (entry);
		free_and_init (vpids);
		return ER_OUT_OF_VIRTUAL_MEMORY;
	      }
	    bt_load_append_ledger_entry (provider, entry);
	  }
	}
      bt_load_add_span (&provider->main_inline_spans, span);
      provider->main_inline_current = span;
    }
  *vpid_new = span->vpids[span->used++];
  provider->consumed_pages++;
  return bt_load_init_claimed_page (thread_p, load_args, header, node_level, vpid_new, page_new);
}

bool
bt_load_parallel_enabled (const LOAD_ARGS * load_args)
{
#if defined (SERVER_MODE)
  return load_args != NULL && load_args->no_redo;
#else
  return false;
#endif
}

void
bt_load_set_px_outcome (LOAD_ARGS * load_args, BT_LOAD_PX_OUTCOME outcome)
{
  assert (load_args->px_outcome == BT_PX_NOT_ATTEMPTED || outcome == BT_PX_NOT_ATTEMPTED);
  load_args->px_outcome = outcome;
}

/*
 * btree_load_new_page () - load a new b-tree page.
 *
 * return          : Error code
 * thread_p (in)   : Thread entry
 * btid (in)       : B-tree identifier
 * header (in)     : Node header for leaf/non-leaf nodes or NULL for overflow OID nodes
 * node_level (in) : Node level for leaf/non-leaf nodes or -1 for overflow OID nodes
 * vpid_new (out)  : Output new page VPID
 * page_new (out)  : Output new page
 */
static int
btree_load_new_page (THREAD_ENTRY * thread_p, const BTID * btid, BTREE_NODE_HEADER * header, int node_level,
		     VPID * vpid_new, PAGE_PTR * page_new)
{
  int error_code = NO_ERROR;

  assert ((header != NULL && node_level >= 1)	/* leaf, non-leaf */
	  || (header == NULL && node_level == -1));	/* overflow */

  /* we need to commit page allocations. if loading index is aborted, the entire file is destroyed. */
  log_sysop_start (thread_p);

  /* allocate new page */
  error_code = file_alloc (thread_p, &btid->vfid, btree_initialize_new_page, NULL, vpid_new, page_new);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }
  if (*page_new == NULL)
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto end;
    }
#if !defined (NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, *page_new, PAGE_BTREE);
#endif /* !NDEBUG */

  if (header)
    {				/* This is going to be a leaf or non-leaf page */
      /* Insert the node header (with initial values) to the leaf node */
      header->node_level = node_level;
      header->max_key_len = 0;
      VPID_SET_NULL (&header->next_vpid);
      VPID_SET_NULL (&header->prev_vpid);
      header->split_info.pivot = 0.0f;
      header->split_info.index = 0;
      header->common_prefix = 0;

      error_code = btree_init_node_header (thread_p, &btid->vfid, *page_new, header, false);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  pgbuf_unfix_and_init (thread_p, *page_new);
	  goto end;
	}
    }
  else
    {				/* This is going to be an overflow page */
      BTREE_OVERFLOW_HEADER ovf_header_info;

      assert (node_level == -1);
      VPID_SET_NULL (&ovf_header_info.next_vpid);

      error_code = btree_init_overflow_header (thread_p, *page_new, &ovf_header_info);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  pgbuf_unfix_and_init (thread_p, *page_new);
	  goto end;
	}
    }

  assert (error_code == NO_ERROR);

end:
  if (error_code != NO_ERROR)
    {
      log_sysop_abort (thread_p);
    }
  else
    {
      log_sysop_commit (thread_p);
    }

  return error_code;
}

/*
 * btree_proceed_leaf () - Proceed the current leaf page to a new one
 *   return: PAGE_PTR (pointer to the new leaf page, or NULL in
 *                     case of an error).
 *   load_args(in): Collection of information on how to build B+tree.
 *
 * Note: This function proceeds the current leaf page pointer from a
 * full leaf page to a new (completely empty) one. It first
 * allocates a new leaf page and connects it to the current
 * leaf page; then, it dumps the current one; and finally makes
 * the new leaf page "current".
 */
static PAGE_PTR
btree_proceed_leaf (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args)
{
  /* Local variables for managing leaf & overflow pages */
  VPID new_leafpgid;
  PAGE_PTR new_leafpgptr = NULL;
  BTREE_NODE_HEADER new_leafhdr, *header = NULL;
  RECDES temp_recdes;		/* Temporary record descriptor; */
  OR_ALIGNED_BUF (sizeof (BTREE_NODE_HEADER)) a_temp_data;

  temp_recdes.data = OR_ALIGNED_BUF_START (a_temp_data);
  temp_recdes.area_size = sizeof (BTREE_NODE_HEADER);

  /* Allocate the new leaf page */
  if ((*load_args->new_page_fn) (thread_p, load_args, &new_leafhdr, 1, &new_leafpgid, &new_leafpgptr) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }
  if (new_leafpgptr == NULL)
    {
      assert_release (false);
      return NULL;
    }

  /* new leaf update header */
  new_leafhdr.prev_vpid = load_args->leaf.vpid;

  header = btree_get_node_header (thread_p, new_leafpgptr);
  if (header == NULL)
    {
      pgbuf_unfix_and_init (thread_p, new_leafpgptr);
      return NULL;
    }

  *header = new_leafhdr;	/* update header */

  /* current leaf update header */
  /* make the current leaf page point to the new one */
  load_args->leaf.hdr.next_vpid = new_leafpgid;

  /* and the new leaf point to the current one */
  header = btree_get_node_header (thread_p, load_args->leaf.pgptr);
  if (header == NULL)
    {
      pgbuf_unfix_and_init (thread_p, new_leafpgptr);
      return NULL;
    }

  *header = load_args->leaf.hdr;

  /* Flush the current leaf page */
  if (btree_log_page (thread_p, &load_args->btid->sys_btid->vfid, load_args->leaf.pgptr, load_args->no_redo,
		      load_args->provider != NULL) != NO_ERROR)
    {
      load_args->leaf.pgptr = NULL;
      pgbuf_unfix_and_init (thread_p, new_leafpgptr);
      return NULL;
    }
  load_args->leaf.pgptr = NULL;

  /* Make the new leaf page become the current one */
  load_args->leaf.vpid = new_leafpgid;
  load_args->leaf.pgptr = new_leafpgptr;
  load_args->leaf.hdr = new_leafhdr;

  return load_args->leaf.pgptr;
}

/*
 * btree_first_oid () - Prepare record for the key and its first oid
 *   return: int
 *   this_key(in): Key
 *   class_oid(in):
 *   first_oid(in): First OID associated with this key; inserted into the
 *                  record.
 *   load_args(in): Contains fields specifying where & how to create the record
 *
 * Note: This function prepares the leaf record for the given key and
 * its first OID at the storage location specified by
 * load_args->out_recdes field.
 */
static int
btree_first_oid (THREAD_ENTRY * thread_p, DB_VALUE * this_key, OID * class_oid, OID * first_oid,
		 MVCC_REC_HEADER * p_mvcc_rec_header, LOAD_ARGS * load_args)
{
  int key_len;
  int key_type;
  int error;
  BTREE_MVCC_INFO mvcc_info;

  assert (load_args->out_recdes == &load_args->leaf_nleaf_recdes);

  /* form the leaf record (create the header & insert the key) */
  key_len = load_args->cur_key_len = btree_get_disk_size_of_key (this_key);
  if (key_len < BTREE_MAX_KEYLEN_INPAGE)
    {
      key_type = BTREE_NORMAL_KEY;
    }
  else
    {
      key_type = BTREE_OVERFLOW_KEY;
      if (VFID_ISNULL (&load_args->btid->ovfid))
	{
	  error = btree_create_overflow_key_file (thread_p, load_args->btid);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }
  btree_mvcc_info_from_heap_mvcc_header (p_mvcc_rec_header, &mvcc_info);
  error =
    bt_load_write_record (thread_p, load_args, NULL, this_key, BTREE_LEAF_NODE, key_type, key_len, class_oid,
			  first_oid, &mvcc_info, load_args->out_recdes);
  if (error != NO_ERROR)
    {
      /* this must be an overflow key insertion failure, we assume the overflow manager has logged an error. */
      return error;
    }

  /* Set the location where the new oid should be inserted */
  load_args->new_pos = (load_args->out_recdes->data + load_args->out_recdes->length);
  assert (load_args->out_recdes->length <= load_args->out_recdes->area_size);

  /* Set the current key value to this_key */
  pr_clear_value (&load_args->current_key);	/* clear previous value */
  load_args->cur_key_len = key_len;

  error = pr_clone_value (this_key, &load_args->current_key);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (MVCC_IS_HEADER_DELID_VALID (p_mvcc_rec_header))
    {
      /* Object is deleted, initialize curr_non_del_obj_count as 0 */
      load_args->curr_non_del_obj_count = 0;
    }
  else
    {
      /* Object was not deleted, increment curr_non_del_obj_count */
      load_args->curr_non_del_obj_count = 1;
      /* Increment the key counter if object is not deleted */
      (load_args->n_keys)++;
    }

  load_args->curr_rec_max_obj_count = BTREE_MAX_OIDCOUNT_IN_LEAF_RECORD (load_args->btid);
  load_args->curr_rec_obj_count = 1;

#if !defined (NDEBUG)
  btree_check_valid_record (thread_p, load_args->btid, load_args->out_recdes, BTREE_LEAF_NODE, NULL);
#endif
  return NO_ERROR;
}


/*
 * bt_load_put_buf_to_record () - Generating temporary records for Index
 *   return: int
 *   recdes(out):
 *   load_args(in): Contains fields specifying where & how to create the record
 *   value_has_null(in):
 *   rec_oid(in): Object identifier of current record.
 *   mvcc_header(in):
 *   dbvalue_ptr(in): Key value
 *   key_len(in):  get_disk_size_of_value(dbvalue_ptr)
 *   cur_class(in):
 *   is_btree_ops_log(in)
 * Note:
 */
static int
bt_load_put_buf_to_record (RECDES * recdes, SORT_ARGS * sort_args, int value_has_null, OID * rec_oid,
			   MVCC_REC_HEADER * mvcc_header, DB_VALUE * dbvalue_ptr, int key_len, int cur_class,
			   bool is_btree_ops_log)
{
  int next_size;
  int record_size;
  OR_BUF buf;
  int oid_size;

  if (BTREE_IS_UNIQUE (sort_args->unique_pk))
    {
      oid_size = 2 * OR_OID_SIZE;
    }
  else
    {
      oid_size = OR_OID_SIZE;
    }

  next_size = sizeof (char *);
  record_size = (next_size	/* Pointer to next */
		 + OR_INT_SIZE	/* Has null */
		 + oid_size	/* OID, Class OID */
		 + 2 * OR_MVCCID_SIZE	/* Insert and delete MVCCID */
		 + key_len	/* Key length */
		 + (int) MAX_ALIGNMENT /* Alignment */ );

  if (recdes->area_size < record_size)
    {
      /*
       * Record is too big to fit into recdes area; so
       * backtrack this iteration
       */
      sort_args->cur_oid = *rec_oid;
      recdes->length = record_size;
      return ER_FAILED;
    }

  assert (PTR_ALIGN (recdes->data, MAX_ALIGNMENT) == recdes->data);
  or_init (&buf, recdes->data, 0);

  or_pad (&buf, next_size);	/* init as NULL */

  /* save has_null */
  if (or_put_byte (&buf, value_has_null) != NO_ERROR)
    {
      return ER_FAILED;
    }

  or_advance (&buf, (OR_INT_SIZE - OR_BYTE_SIZE));
  assert (buf.ptr == PTR_ALIGN (buf.ptr, INT_ALIGNMENT));

  if (or_put_oid (&buf, &sort_args->cur_oid) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (BTREE_IS_UNIQUE (sort_args->unique_pk))
    {
      if (or_put_oid (&buf, &sort_args->class_ids[cur_class]) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  /* Pack insert and delete MVCCID's */
  if (MVCC_IS_HEADER_INSID_NOT_ALL_VISIBLE (mvcc_header))
    {
      if (or_put_mvccid (&buf, MVCC_GET_INSID (mvcc_header)) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      if (or_put_mvccid (&buf, MVCCID_ALL_VISIBLE) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  if (MVCC_IS_HEADER_DELID_VALID (mvcc_header))
    {
      if (or_put_mvccid (&buf, MVCC_GET_DELID (mvcc_header)) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      if (or_put_mvccid (&buf, MVCCID_NULL) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  if (is_btree_ops_log)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "DEBUG_BTREE: load sort found oid(%d, %d, %d)"
		     ", class_oid(%d, %d, %d), btid(%d, (%d, %d), mvcc_info=%llu | %llu.",
		     sort_args->cur_oid.volid, sort_args->cur_oid.pageid, sort_args->cur_oid.slotid,
		     sort_args->class_ids[sort_args->cur_class].volid,
		     sort_args->class_ids[sort_args->cur_class].pageid,
		     sort_args->class_ids[sort_args->cur_class].slotid,
		     sort_args->btid->sys_btid->root_pageid,
		     sort_args->btid->sys_btid->vfid.volid, sort_args->btid->sys_btid->vfid.fileid,
		     MVCC_IS_FLAG_SET (mvcc_header, OR_MVCC_FLAG_VALID_INSID)
		     ? MVCC_GET_INSID (mvcc_header) : MVCCID_ALL_VISIBLE,
		     MVCC_IS_FLAG_SET (mvcc_header, OR_MVCC_FLAG_VALID_DELID)
		     ? MVCC_GET_DELID (mvcc_header) : MVCCID_NULL);
    }

  assert (buf.ptr == PTR_ALIGN (buf.ptr, INT_ALIGNMENT));

  if (sort_args->key_type->type->data_writeval (&buf, dbvalue_ptr) != NO_ERROR)
    {
      return ER_FAILED;
    }

  recdes->length = CAST_STRLEN (buf.ptr - buf.buffer);
  return NO_ERROR;
}

/*
 * bt_load_get_buf_from_record () - Reads records generated by bt_load_put_buf_to_record().
 *   return: int
 *   recdes(in):
 *   load_args(in): Contains fields specifying where & how to create the record
 *   pparam(out): a bundle of record-related information
 *   copy(in):
 * Note: 
 */
static int
bt_load_get_buf_from_record (RECDES * recdes, LOAD_ARGS * load_args, S_PARAM_ST * pparam, bool copy)
{
  OR_BUF buf;
  int ret;
  int next_size = sizeof (char *);

  /* First decompose the input record into the key and oid components */
  or_init (&buf, recdes->data, recdes->length);
  assert (buf.ptr == PTR_ALIGN (buf.ptr, MAX_ALIGNMENT));

  /* Skip forward link, value_has_null */
  or_advance (&buf, next_size + OR_INT_SIZE);

  assert (buf.ptr == PTR_ALIGN (buf.ptr, INT_ALIGNMENT));

  /* Get OID */
  ret = or_get_oid (&buf, &pparam->rec_oid);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  /* Instance level uniqueness checking */
  if (BTREE_IS_UNIQUE (load_args->btid->unique_pk))
    {				/* unique index */
      /* extract class OID */
      ret = or_get_oid (&buf, &pparam->class_oid);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
    }

  /* Create MVCC header */
  BTREE_INIT_MVCC_HEADER (&pparam->mvcc_header);

  ret = or_get_mvccid (&buf, &MVCC_GET_INSID (&pparam->mvcc_header));
  if (ret != NO_ERROR)
    {
      return ret;
    }

  ret = or_get_mvccid (&buf, &MVCC_GET_DELID (&pparam->mvcc_header));
  if (ret != NO_ERROR)
    {
      return ret;
    }

#if defined(SERVER_MODE)
  if (MVCC_GET_INSID (&pparam->mvcc_header) != MVCCID_ALL_VISIBLE)
    {
      /* Set valid insert MVCCID flag */
      MVCC_SET_FLAG_BITS (&pparam->mvcc_header, OR_MVCC_FLAG_VALID_INSID);
    }
  if (MVCC_GET_DELID (&pparam->mvcc_header) != MVCCID_NULL)
    {
      /* Set valid delete MVCCID */
      MVCC_SET_FLAG_BITS (&pparam->mvcc_header, OR_MVCC_FLAG_VALID_DELID);
    }
#else
  /* all inserted OIDs are created as visible in stand-alone */
  MVCC_SET_INSID (&pparam->mvcc_header, MVCCID_ALL_VISIBLE);
  if (MVCC_GET_DELID (&pparam->mvcc_header) != MVCCID_NULL)
    {
      assert (0);
    }
#endif

  assert (buf.ptr == PTR_ALIGN (buf.ptr, INT_ALIGNMENT));

  if (pparam->is_btree_ops_log)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "DEBUG_BTREE: load new object(%d, %d, %d) class(%d, %d, %d) and btid(%d, (%d, %d)) with "
		     "mvccinfo=%llu| %llu", pparam->rec_oid.volid, pparam->rec_oid.pageid, pparam->rec_oid.slotid,
		     pparam->class_oid.volid, pparam->class_oid.pageid, pparam->class_oid.slotid,
		     load_args->btid->sys_btid->root_pageid, load_args->btid->sys_btid->vfid.volid,
		     load_args->btid->sys_btid->vfid.fileid, MVCC_GET_INSID (&pparam->mvcc_header),
		     MVCC_GET_DELID (&pparam->mvcc_header));
    }

  int key_size = -1;

  /* Do not copy the string--just use the pointer.  The pr_ routines for strings and sets have different semantics
   * for length. */
  if (TP_DOMAIN_TYPE (load_args->btid->key_type) == DB_TYPE_MIDXKEY)
    {
      key_size = CAST_STRLEN (buf.endptr - buf.ptr);
    }

  ret =
    load_args->btid->key_type->type->data_readval (&buf, &pparam->this_key, load_args->btid->key_type, key_size, copy,
						   NULL, 0);
  if (ret != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_CORRUPTED, 0);
      return ret;
    }

/* Save OID, class OID and MVCC header since they may be replaced. */
  COPY_OID (&pparam->orig_oid, &pparam->rec_oid);
  COPY_OID (&pparam->orig_class_oid, &pparam->class_oid);
  pparam->orig_mvcc_header = pparam->mvcc_header;

  return NO_ERROR;
}

/*
 * bt_load_get_first_leaf_page_and_init_args () - Create a leaf page and insert the first record
 *   return: int
 *   thread_p(in):
 *   load_args(out): Contains fields specifying where & how to create the record
 *   pparam(in): a bundle of record-related information
 *
 * Note: This is the first call to btree_construct_leafs(). so, initialize some fields in the LOAD_ARGS structure
 */
static int
bt_load_get_first_leaf_page_and_init_args (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, S_PARAM_ST * pparam)
{
  /* Allocate the first page for the index */
  int ret = (*load_args->new_page_fn) (thread_p, load_args, &load_args->leaf.hdr, 1, &load_args->leaf.vpid,
				       &load_args->leaf.pgptr);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      return ret;
    }
  if (load_args->leaf.pgptr == NULL || VPID_ISNULL (&load_args->leaf.vpid))
    {
      assert_release (false);
      if ((ret = er_errid ()) == NO_ERROR)
	{
	  return ER_FAILED;
	}
      return ret;
    }

  /* Save first leaf VPID. */
  load_args->vpid_first_leaf = load_args->leaf.vpid;
  load_args->overflowing = false;
  assert (load_args->ovf.pgptr == NULL);

  /* Create the first record of the current page in main memory */
  load_args->out_recdes = &load_args->leaf_nleaf_recdes;
  return btree_first_oid (thread_p, &pparam->this_key, &pparam->class_oid, &pparam->rec_oid, &pparam->mvcc_header,
			  load_args);
}

/*
 * bt_load_make_new_record_on_leaf_page () - Create a new record from the leaf page
 *   return: int
 *   thread_p(in):
 *   load_args(in): Contains fields specifying where & how to create the record
 *   pparam(in): a bundle of record-related information
 *   sp_success(out): When this value is set to a value other than SP_SUCCESS,
 *                    error processing is performed without changing the error code.
 *
 * Note:
 */
static int
bt_load_make_new_record_on_leaf_page (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, S_PARAM_ST * pparam,
				      int *sp_success)
{
  int cur_maxspace;
  int ret = NO_ERROR;
  /* Current key is finished; dump this output record to the disk page */

  /* Insert current leaf record */
  cur_maxspace = spage_max_space_for_new_record (thread_p, load_args->leaf.pgptr);
  if (((cur_maxspace - load_args->leaf_nleaf_recdes.length) < LOAD_FIXED_EMPTY_FOR_LEAF)
      && (spage_number_of_records (load_args->leaf.pgptr) > 1))
    {
      /* New record does not fit into the current leaf page (within the threshold value); so allocate a new
       * leaf page and dump the current leaf page. */
      if (btree_proceed_leaf (thread_p, load_args) == NULL)
	{
	  *sp_success = ER_FAILED;
	  return ret;
	}
    }				/* get a new leaf */

  /* Insert the record to the current leaf page */
  *sp_success =
    spage_insert (thread_p, load_args->leaf.pgptr, &load_args->leaf_nleaf_recdes, &load_args->last_leaf_insert_slotid);
  if (*sp_success != SP_SUCCESS)
    {
      return ret;
    }

  assert (load_args->last_leaf_insert_slotid > 0);

  /* Update the node header information for this record */
  if (load_args->cur_key_len >= BTREE_MAX_KEYLEN_INPAGE)
    {
      if (load_args->leaf.hdr.max_key_len < DISK_VPID_SIZE)
	{
	  load_args->leaf.hdr.max_key_len = DISK_VPID_SIZE;
	}
    }
  else if (load_args->leaf.hdr.max_key_len < load_args->cur_key_len)
    {
      load_args->leaf.hdr.max_key_len = load_args->cur_key_len;
    }
  load_args->report_local_max_key_len =
    MAX (load_args->report_local_max_key_len, (int) load_args->leaf.hdr.max_key_len);

  if (load_args->overflowing)
    {
      /* Insert the new record to the current overflow page and flush this page */
      INT16 slotid;

      assert (load_args->out_recdes == &load_args->ovf_recdes);

      /* Store the record in current overflow page */
      *sp_success = spage_insert (thread_p, load_args->ovf.pgptr, load_args->out_recdes, &slotid);
      if (*sp_success != SP_SUCCESS)
	{
	  return ret;
	}

      assert (slotid > 0);

      /* Save the current overflow page */
      ret = btree_log_page (thread_p, &load_args->btid->sys_btid->vfid, load_args->ovf.pgptr, load_args->no_redo,
			    load_args->provider != NULL);
      load_args->ovf.pgptr = NULL;
      if (ret != NO_ERROR)
	{
	  return ret;
	}

      /* Turn off the overflowing mode */
      load_args->overflowing = false;
    }				/* Current page is an overflow page */
  else
    {
      assert (load_args->out_recdes == &load_args->leaf_nleaf_recdes);
    }

  /* Create the first part of the next record in main memory */
  load_args->out_recdes = &load_args->leaf_nleaf_recdes;
  return btree_first_oid (thread_p, &pparam->this_key, &pparam->class_oid, &pparam->rec_oid, &pparam->mvcc_header,
			  load_args);
}


/*
 * bt_load_invalidate_mvcc_del_id () -
 *   return: int
 *   thread_p(in):
 *   load_args(in): Contains fields specifying where & how to create the record
 *   pparam(in): a bundle of record-related information
 * Note:
 */
static int
bt_load_invalidate_mvcc_del_id (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, S_PARAM_ST * pparam)
{
  int ret;

  /* TODO: Rewrite btree_construct_leafs. It's almost impossible to follow. */
  load_args->curr_non_del_obj_count++;
  if (load_args->curr_non_del_obj_count == 1)
    {
      /* When first non-deleted object is found, we must increment that number of keys for statistics. */
      (load_args->n_keys)++;

      if (BTREE_IS_UNIQUE (load_args->btid->unique_pk))
	{
	  /* this is the first non-deleted OID of the key; it must be placed as the first OID */
	  BTREE_MVCC_INFO first_mvcc_info;
	  OID first_oid, first_class_oid;
	  int offset = 0;

	  /* Retrieve the first OID from leaf record */
	  ret =
	    btree_leaf_get_first_object (load_args->btid, &load_args->leaf_nleaf_recdes, &first_oid,
					 &first_class_oid, &first_mvcc_info);
	  if (ret != NO_ERROR)
	    {
	      return ret;
	    }

	  /* replace with current OID (might move memory in record) */
	  btree_mvcc_info_from_heap_mvcc_header (&pparam->mvcc_header, &pparam->mvcc_info);
	  btree_leaf_change_first_object (thread_p, &load_args->leaf_nleaf_recdes, load_args->btid,
					  &pparam->rec_oid, &pparam->class_oid, &pparam->mvcc_info, &offset, NULL,
					  NULL);
	  if (ret != NO_ERROR)
	    {
	      return ret;
	    }

	  if (!load_args->overflowing)
	    {
	      /* Update load_args->new_pos in case record has grown after replace */
	      load_args->new_pos += offset;
	    }

	  assert (load_args->leaf_nleaf_recdes.length <= load_args->leaf_nleaf_recdes.area_size);
#if !defined (NDEBUG)
	  btree_check_valid_record (thread_p, load_args->btid, &load_args->leaf_nleaf_recdes, BTREE_LEAF_NODE, NULL);
#endif

	  /* save first OID as current OID, to be written */
	  COPY_OID (&pparam->rec_oid, &first_oid);
	  COPY_OID (&pparam->class_oid, &first_class_oid);
	  btree_mvcc_info_to_heap_mvcc_header (&first_mvcc_info, &pparam->mvcc_header);
	}
    }
  else if (load_args->curr_non_del_obj_count > 1 && BTREE_IS_UNIQUE (load_args->btid->unique_pk))
    {
      /* Unique constrain violation - more than one visible records for this key. */
      BTREE_SET_UNIQUE_VIOLATION_ERROR (thread_p, &pparam->this_key, &pparam->rec_oid, &pparam->class_oid,
					load_args->btid->sys_btid, load_args->bt_name);
      return ER_BTREE_UNIQUE_FAILED;
    }

  return NO_ERROR;
}


/*
 * bt_load_nospace_for_new_oid () - Create and save the Overflow OID page.
 *   return: int
 *   thread_p(in):
 *   load_args(in): Contains fields specifying where & how to create the record
 *   sp_success(out): When this value is set to a value other than SP_SUCCESS,
 *                    error processing is performed without changing the error code.
 * Note:
 */
static int
bt_load_nospace_for_new_oid (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, int *sp_success)
{
  int ret;
  PAGE_PTR new_ovfpgptr = NULL;

  assert (*sp_success == SP_SUCCESS);

  /* There is no space for the new oid */
  if (load_args->overflowing == true)
    {
      INT16 slotid;
      BTREE_OVERFLOW_HEADER *ovf_header = NULL;
      VPID new_ovfpgid;

      /* Store the record in current overflow page */
      assert (load_args->out_recdes == &load_args->ovf_recdes);
      *sp_success = spage_insert (thread_p, load_args->ovf.pgptr, &load_args->ovf_recdes, &slotid);
      if (*sp_success != SP_SUCCESS)
	{
	  return NO_ERROR;
	}

      assert (slotid > 0);

      /* Allocate the new overflow page */
      ret = (*load_args->new_page_fn) (thread_p, load_args, NULL, -1, &new_ovfpgid, &new_ovfpgptr);
      if (ret != NO_ERROR)
	{
	  pgbuf_unfix_and_init_after_check (thread_p, new_ovfpgptr);
	  ASSERT_ERROR ();
	  return ret;
	}
      if (new_ovfpgptr == NULL)
	{
	  pgbuf_unfix_and_init_after_check (thread_p, new_ovfpgptr);
	  assert_release (false);
	  return ER_FAILED;
	}

      /* make the current overflow page point to the new one */
      ovf_header = btree_get_overflow_header (thread_p, load_args->ovf.pgptr);
      if (ovf_header == NULL)
	{
	  pgbuf_unfix_and_init_after_check (thread_p, new_ovfpgptr);
	  *sp_success = ER_FAILED;
	  return ret;
	}

      ovf_header->next_vpid = new_ovfpgid;

      /* Save the current overflow page */
      ret = btree_log_page (thread_p, &load_args->btid->sys_btid->vfid, load_args->ovf.pgptr, load_args->no_redo,
			    load_args->provider != NULL);
      load_args->ovf.pgptr = NULL;
      if (ret != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, new_ovfpgptr);
	  return ret;
	}

      /* Make the new overflow page become the current one */
      load_args->ovf.vpid = new_ovfpgid;
      load_args->ovf.pgptr = new_ovfpgptr;
      new_ovfpgptr = NULL;
    }
  else
    {				/* Current page is a leaf page */
      assert (load_args->out_recdes == &load_args->leaf_nleaf_recdes);
      /* Allocate the new overflow page */
      ret = (*load_args->new_page_fn) (thread_p, load_args, NULL, -1, &load_args->ovf.vpid, &load_args->ovf.pgptr);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return ret;
	}
      if (load_args->ovf.pgptr == NULL)
	{
	  assert_release (false);
	  *sp_success = ER_FAILED;
	  return NO_ERROR;
	}

      /* Connect the new overflow page to the leaf page */
      btree_leaf_record_change_overflow_link (thread_p, load_args->btid, &load_args->leaf_nleaf_recdes,
					      &load_args->ovf.vpid, NULL, NULL);

      load_args->overflowing = true;

      /* Set out_recdes to ovf_recdes. */
      load_args->out_recdes = &load_args->ovf_recdes;
    }				/* Current page is a leaf page */

  /* Initialize the memory area for the next record */
  assert (load_args->out_recdes == &load_args->ovf_recdes);
  load_args->out_recdes->length = 0;
  load_args->new_pos = load_args->out_recdes->data;
  load_args->curr_rec_max_obj_count =
    BTREE_MAX_OIDCOUNT_IN_SIZE (load_args->btid, spage_max_space_for_new_record (thread_p, load_args->ovf.pgptr));
  load_args->curr_rec_obj_count = 1;

  return NO_ERROR;
}

/*
 * bt_load_add_same_key_to_record () - Insert the same key into an existing record
 *   return: int
 *   thread_p(in):
 *   load_args(in): Contains fields specifying where & how to create the record
 *   pparam(in): a bundle of record-related information
 *   sp_success(out): When this value is set to a value other than SP_SUCCESS,
 *                    error processing is performed without changing the error code.
 *
 * Note:
 */
static int
bt_load_add_same_key_to_record (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, S_PARAM_ST * pparam, int *sp_success)
{
  int ret;

  assert (*sp_success == SP_SUCCESS);

  /* This key (retrieved key) is the same with the current one. */
  load_args->curr_rec_obj_count++;
  if (!MVCC_IS_HEADER_DELID_VALID (&pparam->mvcc_header))
    {
      ret = bt_load_invalidate_mvcc_del_id (thread_p, load_args, pparam);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
    }

  /* Check if there is space in the memory record for the new OID. If not dump the current record and
   * create a new record. */
  if (load_args->curr_rec_obj_count > load_args->curr_rec_max_obj_count)
    {				/* no space for the new OID */
      ret = bt_load_nospace_for_new_oid (thread_p, load_args, sp_success);
      if (ret != NO_ERROR || *sp_success != SP_SUCCESS)
	{
	  return ret;
	}
    }

  if (load_args->overflowing || BTREE_IS_UNIQUE (load_args->btid->unique_pk))
    {
      /* all overflow OIDs have fixed header size; also, all OIDs of a unique index key (except the first
       * one) have fixed size */
      BTREE_MVCC_SET_HEADER_FIXED_SIZE (&pparam->mvcc_header);
    }

  /* Insert new OID, class OID (for unique), and MVCCID's. */
  /* Insert OID (and MVCC flags) */
  btree_set_mvcc_flags_into_oid (&pparam->mvcc_header, &pparam->rec_oid);
  OR_PUT_OID (load_args->new_pos, &pparam->rec_oid);
  load_args->out_recdes->length += OR_OID_SIZE;
  load_args->new_pos += OR_OID_SIZE;

  if (BTREE_IS_UNIQUE (load_args->btid->unique_pk))
    {				/* Insert class OID */
      OR_PUT_OID (load_args->new_pos, &pparam->class_oid);
      load_args->out_recdes->length += OR_OID_SIZE;
      load_args->new_pos += OR_OID_SIZE;
    }

  btree_mvcc_info_from_heap_mvcc_header (&pparam->mvcc_header, &pparam->mvcc_info);
  /* Insert MVCCID's */
  load_args->out_recdes->length += btree_packed_mvccinfo_size (&pparam->mvcc_info);
  load_args->new_pos = btree_pack_mvccinfo (load_args->new_pos, &pparam->mvcc_info);

  assert (load_args->out_recdes->length <= load_args->out_recdes->area_size);

#if !defined (NDEBUG)
  btree_check_valid_record (thread_p, load_args->btid, load_args->out_recdes,
			    (load_args->overflowing ? BTREE_OVERFLOW_NODE : BTREE_LEAF_NODE), NULL);
#endif
  return NO_ERROR;
}

/*
 * bt_load_notify_to_vacuum () -
 *   return: int
 *   thread_p(in):
 *   load_args(in): Contains fields specifying where & how to create the record
 *   pparam(in): a bundle of record-related information
 *   notify_vacuum_rv_data(in):
 *   notify_vacuum_rv_data_bufalign(in):
 *
 * Note:
 */
static int
bt_load_notify_to_vacuum (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, S_PARAM_ST * pparam,
			  char **notify_vacuum_rv_data, char *notify_vacuum_rv_data_bufalign)
{
  int ret;

  if (pparam->is_btree_ops_log)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "DEBUG_BTREE: load added object(%d, %d, %d) "
		     "class(%d, %d, %d) and btid(%d, (%d, %d)) with mvccinfo=%llu | %llu", pparam->rec_oid.volid,
		     pparam->rec_oid.pageid, pparam->rec_oid.slotid, pparam->class_oid.volid,
		     pparam->class_oid.pageid, pparam->class_oid.slotid,
		     load_args->btid->sys_btid->root_pageid, load_args->btid->sys_btid->vfid.volid,
		     load_args->btid->sys_btid->vfid.fileid, MVCC_GET_INSID (&pparam->mvcc_header),
		     MVCC_GET_DELID (&pparam->mvcc_header));
    }

  /* Some objects have been recently deleted and couldn't be filtered (because there may be running transaction
   * that can still see them. However, they must be vacuumed later. Vacuum can only find them by parsing log,
   * therefore some log records are required. These are dummy records with the sole purpose of notifying vacuum.
   * Since rec_oid, class_oid and mvcc_header could be replaced in case first object of unique index had to
   * be swapped, we will use orig_oid, orig_class_oid and orig_mvcc_header to log object load for
   * vacuum. */
  if (MVCC_IS_HEADER_DELID_VALID (&pparam->orig_mvcc_header)
      || MVCC_IS_HEADER_INSID_NOT_ALL_VISIBLE (&pparam->orig_mvcc_header))
    {
      /* There is something to vacuum for this object */
      int notify_vacuum_rv_data_capacity = IO_MAX_PAGE_SIZE;
      int notify_vacuum_rv_data_length = 0;
      char *pgptr = (load_args->overflowing ? load_args->ovf.pgptr : load_args->leaf.pgptr);

      /* clear flags for logging */
      btree_clear_mvcc_flags_from_oid (&pparam->orig_oid);

      if (pparam->is_btree_ops_log)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "DEBUG_BTREE: load notify vacuum object(%d, %d, %d) "
			 "class(%d, %d, %d) and btid(%d, (%d, %d)) with mvccinfo=%llu | %llu",
			 pparam->orig_oid.volid, pparam->orig_oid.pageid, pparam->orig_oid.slotid,
			 pparam->orig_class_oid.volid, pparam->orig_class_oid.pageid, pparam->orig_class_oid.slotid,
			 load_args->btid->sys_btid->root_pageid, load_args->btid->sys_btid->vfid.volid,
			 load_args->btid->sys_btid->vfid.fileid, MVCC_GET_INSID (&pparam->orig_mvcc_header),
			 MVCC_GET_DELID (&pparam->orig_mvcc_header));
	}

      /* append log data */
      btree_mvcc_info_from_heap_mvcc_header (&pparam->orig_mvcc_header, &pparam->mvcc_info);
      ret =
	btree_rv_save_keyval_for_undo (load_args->btid, &pparam->this_key, &pparam->orig_class_oid, &pparam->orig_oid,
				       &pparam->mvcc_info, BTREE_OP_NOTIFY_VACUUM, notify_vacuum_rv_data_bufalign,
				       notify_vacuum_rv_data, &notify_vacuum_rv_data_capacity,
				       &notify_vacuum_rv_data_length);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
      size_t new_capacity = load_args->vacuum_capacity == 0 ? 16 : load_args->vacuum_capacity * 2;
      size_t reserved_size;
      BT_LOAD_VACUUM_ITEM *new_items;
      char *data;

      if (load_args->vacuum_count == load_args->vacuum_capacity)
	{
	  if (new_capacity < load_args->vacuum_capacity || new_capacity > SIZE_MAX / sizeof (*new_items))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, SIZE_MAX);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	}
      else
	{
	  new_capacity = load_args->vacuum_capacity;
	}

      reserved_size = new_capacity * sizeof (*new_items);
      if ((size_t) notify_vacuum_rv_data_length > BT_LOAD_VACUUM_SLOT_LIMIT
	  || load_args->vacuum_payload_size > BT_LOAD_VACUUM_SLOT_LIMIT - (size_t) notify_vacuum_rv_data_length
	  || reserved_size > BT_LOAD_VACUUM_SLOT_LIMIT
	  || load_args->vacuum_payload_size + (size_t) notify_vacuum_rv_data_length
	  > BT_LOAD_VACUUM_SLOT_LIMIT - reserved_size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, BT_LOAD_VACUUM_SLOT_LIMIT);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      if (new_capacity != load_args->vacuum_capacity)
	{
	  new_items = (BT_LOAD_VACUUM_ITEM *) realloc (load_args->vacuum_items, reserved_size);
	  if (new_items == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, reserved_size);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  load_args->vacuum_items = new_items;
	  load_args->vacuum_capacity = new_capacity;
	}

      data = (char *) malloc ((size_t) notify_vacuum_rv_data_length);
      if (data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, notify_vacuum_rv_data_length);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      memcpy (data, *notify_vacuum_rv_data, (size_t) notify_vacuum_rv_data_length);
      load_args->vacuum_items[load_args->vacuum_count].data = data;
      load_args->vacuum_items[load_args->vacuum_count].length = notify_vacuum_rv_data_length;
      load_args->vacuum_count++;
      load_args->vacuum_payload_size += (size_t) notify_vacuum_rv_data_length;
      pgbuf_set_dirty (thread_p, pgptr, DONT_FREE);
    }

  return NO_ERROR;
}

static void
bt_load_clear_vacuum_notifications (LOAD_ARGS * load_args)
{
  size_t i;

  for (i = 0; i < load_args->vacuum_count; i++)
    {
      free (load_args->vacuum_items[i].data);
    }
  free (load_args->vacuum_items);
  load_args->vacuum_items = NULL;
  load_args->vacuum_count = 0;
  load_args->vacuum_capacity = 0;
  load_args->vacuum_payload_size = 0;
}

static int
bt_load_append_vacuum_notifications (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args)
{
  size_t i;
  for (i = 0; i < load_args->vacuum_count; i++)
    {
      int error_before = er_errid ();
      if (error_before != NO_ERROR)
	{
	  return error_before;
	}
      log_append_undo_data2 (thread_p, RVBT_MVCC_NOTIFY_VACUUM, &load_args->btid->sys_btid->vfid, NULL, -1,
			     load_args->vacuum_items[i].length, load_args->vacuum_items[i].data);
      if (er_errid () != NO_ERROR)
	{
	  return er_errid ();
	}
    }
  return NO_ERROR;
}

/*
 * btree_construct_leafs () - Output function for index sorting;constructs leaf
 *                         nodes
 *   return: int
 *   in_recdes(in): specifies the next sort item in the sorting order.
 *   arg(in): output arguments; provides information about how to use the
 *            next item in the sorted list to construct the leaf nodes of
 *            the Btree.
 *
 * Note: This function creates the btree leaf nodes. It is passed
 * by the "btree_index_sort" function to the "sort_listfile" function
 * to use the sort items to build the records and pages of the btree.
 */
static int
btree_construct_leafs (THREAD_ENTRY * thread_p, const RECDES * in_recdes, void *arg)
{
  int ret = NO_ERROR;
  bool copy = false;
  RECDES sort_key_recdes, *recdes;
  char *next;
  char notify_vacuum_rv_data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  char *notify_vacuum_rv_data_bufalign = PTR_ALIGN (notify_vacuum_rv_data_buffer, BTREE_MAX_ALIGN);
  char *notify_vacuum_rv_data = notify_vacuum_rv_data_bufalign;

  LOAD_ARGS *load_args = (LOAD_ARGS *) arg;
  S_PARAM_ST sparam;
  sparam.class_oid = oid_Null_oid;
  sparam.rec_oid = oid_Null_oid;
  sparam.is_btree_ops_log = prm_get_bool_value (PRM_ID_LOG_BTREE_OPS);
  sparam.orig_oid = oid_Null_oid;
  sparam.orig_class_oid = oid_Null_oid;

#if defined (SERVER_MODE)
  assert (load_args->build_mvccid != MVCCID_NULL);
#endif /* SERVER_MODE */

  sort_key_recdes = *in_recdes;
  recdes = &sort_key_recdes;

  for (;;)
    {				/* Infinite loop; will exit with break statement */
      next = *(char **) recdes->data;	/* save forward link */
      load_args->report_n_oids++;
      if (OR_GET_BYTE (recdes->data + sizeof (char *)) != 0)
	{
	  load_args->report_n_nulls++;
	}
      if ((ret = bt_load_get_buf_from_record (recdes, load_args, &sparam, copy)) != NO_ERROR)
	{
	  goto error;
	}

      if (VPID_ISNULL (&(load_args->leaf.vpid)))	/* Find out if this is the first call to this function */
	{
	  /* This is the first call to this function; so, initialize some fields in the LOAD_ARGS structure */
	  if ((ret = bt_load_get_first_leaf_page_and_init_args (thread_p, load_args, &sparam)) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else
	{			/* This is not the first call to this function */
	  /*
	   * Compare the received key with the current one.
	   * If different, then dump the current record and create a new record.
	   */
	  int sp_success = SP_SUCCESS;
	  int c = btree_compare_key (&sparam.this_key, &load_args->current_key, load_args->btid->key_type, 0, 1, NULL);
	  /* EQUALITY test only - doesn't care the reverse index */
	  if (c == DB_GT)
	    {			/* Current key is finished; dump this output record to the disk page */
	      ret = bt_load_make_new_record_on_leaf_page (thread_p, load_args, &sparam, &sp_success);
	    }
	  else if (c == DB_EQ)
	    {			/* This key (retrieved key) is the same with the current one. */
	      ret = bt_load_add_same_key_to_record (thread_p, load_args, &sparam, &sp_success);
	    }
	  else
	    {
	      assert_release (false);
	      goto error;
	    }

	  if (ret != NO_ERROR || sp_success != SP_SUCCESS)
	    {
	      goto error;
	    }
	}

      ret =
	bt_load_notify_to_vacuum (thread_p, load_args, &sparam, &notify_vacuum_rv_data, notify_vacuum_rv_data_bufalign);
      if (ret != NO_ERROR)
	{
	  goto error;
	}

      /* set level 1 to leaf */
      load_args->leaf.hdr.node_level = 1;
      if (sparam.this_key.need_clear)
	{
	  copy = true;
	}

      btree_clear_key_value (&copy, &sparam.this_key);

      if (!next)
	{
	  break;		/* exit infinite loop */
	}

      /* move to next link */
      recdes->data = next;
      recdes->length = SORT_RECORD_LENGTH (next);
    }				// for (;;)

  if (notify_vacuum_rv_data != NULL && notify_vacuum_rv_data != notify_vacuum_rv_data_bufalign)
    {
      db_private_free (thread_p, notify_vacuum_rv_data);
    }

  assert (ret == NO_ERROR);
  return ret;

error:
  pgbuf_unfix_and_init_after_check (thread_p, load_args->leaf.pgptr);
  pgbuf_unfix_and_init_after_check (thread_p, load_args->ovf.pgptr);

  btree_clear_key_value (&copy, &sparam.this_key);

  if (notify_vacuum_rv_data != NULL && notify_vacuum_rv_data != notify_vacuum_rv_data_bufalign)
    {
      db_private_free (thread_p, notify_vacuum_rv_data);
    }

  assert (er_errid () != NO_ERROR);
  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

static int
bt_load_claim_ovf_page (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, VPID * vpid)
{
  BT_LOAD_PROVIDER *provider = load_args->provider;
  BT_LOAD_SPAN *span;
  int error;

  if (load_args->worker_idx >= 0)
    {
      return bt_load_provider_claim_page (provider, load_args->worker_idx, true, vpid);
    }

  span = provider->ovf_inline_current;
  if (span == NULL || span->used == span->n)
    {
      error = bt_load_claim_pool_span (&provider->ovf_pool, true, &span);
      if (error != NO_ERROR)
	{
	  VPID *vpids = NULL;
	  error = bt_load_allocate_span (thread_p, &provider->ovf_vfid, true, 64, &vpids);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  {
	    BT_LOAD_ALLOCATION_LEDGER *entry =
	      bt_load_make_ledger_entry (&provider->ovf_vfid, vpids, 64, BT_LOAD_ORIGIN_MAIN_INLINE);
	    span = bt_load_make_span (vpids, 64, true, true);
	    if (span == NULL || entry == NULL)
	      {
		free_and_init (span);
		bt_load_free_ledger_entry (entry);
		free_and_init (vpids);
		return ER_OUT_OF_VIRTUAL_MEMORY;
	      }
	    bt_load_append_ledger_entry (provider, entry);
	  }
	}
      bt_load_add_span (&provider->ovf_inline_spans, span);
      provider->ovf_inline_current = span;
    }
  *vpid = span->vpids[span->used++];
  provider->consumed_pages++;
  provider->consumed_ovf_pages++;
  return NO_ERROR;
}

static int
bt_load_store_overflow_key (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, DB_VALUE * key, int key_len,
			    BTREE_NODE_TYPE node_type, VPID * first_vpid)
{
  TP_DOMAIN *domain = node_type == BTREE_LEAF_NODE ? load_args->btid->key_type : load_args->btid->nonleaf_key_type;
  DB_VALUE converted, *key_ptr = key;
  RECDES rec = RECDES_INITIALIZER;
  OR_BUF buf;
  VPID *vpids = NULL;
  int remaining, npages, error = NO_ERROR;

  if (DB_VALUE_DOMAIN_TYPE (key) != domain->type->id)
    {
      db_make_null (&converted);
      if (tp_value_cast (key, &converted, domain, false) != DOMAIN_COMPATIBLE)
	{
	  return ER_FAILED;
	}
      key_ptr = &converted;
      key_len = btree_get_disk_size_of_key (key_ptr);
    }
  rec.area_size = key_len;
  rec.data = (char *) db_private_alloc (thread_p, (size_t) key_len);
  if (rec.data == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }
  or_init (&buf, rec.data, rec.area_size);
  error = domain->type->index_writeval (&buf, key_ptr);
  if (error != NO_ERROR)
    {
      goto end;
    }
  rec.length = CAST_BUFLEN (buf.ptr - buf.buffer);
  remaining = rec.length - (DB_PAGESIZE - (int) offsetof (OVERFLOW_FIRST_PART, data));
  npages = 1;
  if (remaining > 0)
    {
      npages += CEIL_PTVDIV (remaining, DB_PAGESIZE - (int) offsetof (OVERFLOW_REST_PART, data));
    }
  vpids = (VPID *) malloc ((size_t) (npages + 1) * sizeof (VPID));
  if (vpids == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }
  for (int i = 0; i < npages; i++)
    {
      error = bt_load_claim_ovf_page (thread_p, load_args, &vpids[i]);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }
  VPID_SET_NULL (&vpids[npages]);
  *first_vpid = vpids[0];

  {
    char *data = rec.data;
    int length = rec.length;
    for (int i = 0; i < npages; i++)
      {
	PAGE_PTR page = pgbuf_fix (thread_p, &vpids[i], OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	char *target;
	int capacity, copy_length;
	if (page == NULL)
	  {
	    error = er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
	    goto end;
	  }
	if (i == 0)
	  {
	    OVERFLOW_FIRST_PART *first = (OVERFLOW_FIRST_PART *) page;
	    first->next_vpid = vpids[i + 1];
	    first->length = length;
	    target = (char *) first->data;
	    capacity = DB_PAGESIZE - (int) offsetof (OVERFLOW_FIRST_PART, data);
	  }
	else
	  {
	    OVERFLOW_REST_PART *rest = (OVERFLOW_REST_PART *) page;
	    rest->next_vpid = vpids[i + 1];
	    target = (char *) rest->data;
	    capacity = DB_PAGESIZE - (int) offsetof (OVERFLOW_REST_PART, data);
	  }
	copy_length = MIN (capacity, length);
	memcpy (target, data, (size_t) copy_length);
	error = btree_log_page (thread_p, &load_args->btid->ovfid, page, true, true);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }
	data += copy_length;
	length -= copy_length;
      }
  }

end:
  if (vpids != NULL)
    {
      free_and_init (vpids);
    }
  if (rec.data != NULL)
    {
      db_private_free_and_init (thread_p, rec.data);
    }
  if (key_ptr != key)
    {
      pr_clear_value (key_ptr);
    }
  return error;
}

static void
bt_load_leaf_set_flag (RECDES * rec, short flag)
{
  short slotid = OR_GET_SHORT (rec->data + OR_OID_SLOTID);
  OR_PUT_SHORT (rec->data + OR_OID_SLOTID, slotid | flag);
}

static void
bt_load_object_set_mvcc_flags (char *data, short flags)
{
  short volid = OR_GET_SHORT (data + OR_OID_VOLID);
  OR_PUT_SHORT (data + OR_OID_VOLID, volid | flags);
}

static int
bt_load_write_record (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, void *node_rec, DB_VALUE * key,
		      BTREE_NODE_TYPE node_type, int key_type, int key_len, OID * class_oid, OID * oid,
		      BTREE_MVCC_INFO * mvcc_info, RECDES * rec)
{
  OR_BUF buf;
  VPID key_vpid;
  int error;

  if (key_type != BTREE_OVERFLOW_KEY || load_args->provider == NULL)
    {
      return btree_write_record (thread_p, load_args->btid, node_rec, key, node_type, key_type, key_len, true,
				 class_oid, oid, mvcc_info, rec);
    }

  or_init (&buf, rec->data, rec->area_size);
  if (node_type == BTREE_LEAF_NODE)
    {
      error = or_put_oid (&buf, oid);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (BTREE_IS_UNIQUE (load_args->btid->unique_pk) && !OID_EQ (&load_args->btid->topclass_oid, class_oid))
	{
	  error = or_put_oid (&buf, class_oid);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  bt_load_leaf_set_flag (rec, (short) 0x8000);
	}
      if (mvcc_info != NULL)
	{
	  if ((mvcc_info->flags & (short) 0x4000) != 0)
	    {
	      if ((error = or_put_mvccid (&buf, mvcc_info->insert_mvccid)) != NO_ERROR)
		{
		  return error;
		}
	    }
	  if ((mvcc_info->flags & (short) 0x8000) != 0)
	    {
	      if ((error = or_put_mvccid (&buf, mvcc_info->delete_mvccid)) != NO_ERROR)
		{
		  return error;
		}
	    }
	  bt_load_object_set_mvcc_flags (rec->data, mvcc_info->flags);
	}
      bt_load_leaf_set_flag (rec, (short) 0x4000);
    }
  else
    {
      NON_LEAF_REC *non_leaf = (NON_LEAF_REC *) node_rec;
      if ((error = or_put_int (&buf, non_leaf->pnt.pageid)) != NO_ERROR
	  || (error = or_put_short (&buf, non_leaf->pnt.volid)) != NO_ERROR
	  || (error = or_put_short (&buf, non_leaf->key_len)) != NO_ERROR)
	{
	  return error;
	}
    }

  error = bt_load_store_overflow_key (thread_p, load_args, key, key_len, node_type, &key_vpid);
  if (error != NO_ERROR)
    {
      return error;
    }
  if ((error = or_put_int (&buf, key_vpid.pageid)) != NO_ERROR
      || (error = or_put_short (&buf, key_vpid.volid)) != NO_ERROR || (error = or_put_align32 (&buf)) != NO_ERROR)
    {
      return error;
    }
  rec->length = CAST_BUFLEN (buf.ptr - buf.buffer);
  rec->type = REC_HOME;
  load_args->report_overflow_key_count++;
  return NO_ERROR;
}

int
bt_load_alloc_shard_load_args (THREAD_ENTRY * thread_p, const LOAD_ARGS * src, BT_LOAD_PROVIDER * provider,
			       int worker_idx, LOAD_ARGS ** out)
{
  LOAD_ARGS *load_args;
  int error;

  if (src == NULL || provider == NULL || out == NULL || worker_idx < 0 || worker_idx >= provider->n_workers)
    {
      return ER_FAILED;
    }
  if (worker_idx == 0 && VFID_ISNULL (&provider->ovf_vfid))
    {
      provider->ovf_vfid = src->btid->ovfid;
      if (VFID_ISNULL (&provider->ovf_vfid))
	{
	  return ER_FAILED;
	}
      if (provider->est_ovf_pages > 0)
	{
	  provider->ovf_pool.n_published = provider->est_ovf_pages;
	  error = bt_load_allocate_span (thread_p, &provider->ovf_vfid, true, provider->ovf_pool.n_published,
					 &provider->ovf_pool.vpids);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  {
	    BT_LOAD_ALLOCATION_LEDGER *entry = bt_load_make_ledger_entry (&provider->ovf_vfid, provider->ovf_pool.vpids,
									  provider->ovf_pool.n_published,
									  BT_LOAD_ORIGIN_POOL_INIT);
	    if (entry == NULL)
	      {
		return ER_OUT_OF_VIRTUAL_MEMORY;
	      }
	    bt_load_append_ledger_entry (provider, entry);
	  }
	}
    }

  load_args = (LOAD_ARGS *) malloc (sizeof (*load_args));
  if (load_args == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memcpy (load_args, src, sizeof (*load_args));
  load_args->provider = provider;
  load_args->worker_idx = worker_idx;
  load_args->new_page_fn = bt_load_new_page_from_provider;
  load_args->px_outcome = BT_PX_NOT_ATTEMPTED;
  load_args->push_list = NULL;
  load_args->pop_list = NULL;
  load_args->out_recdes = NULL;
  load_args->nleaf.pgptr = NULL;
  load_args->leaf.pgptr = NULL;
  load_args->ovf.pgptr = NULL;
  VPID_SET_NULL (&load_args->nleaf.vpid);
  VPID_SET_NULL (&load_args->leaf.vpid);
  VPID_SET_NULL (&load_args->ovf.vpid);
  VPID_SET_NULL (&load_args->vpid_first_leaf);
  VPID_SET_NULL (&load_args->report_first_leaf_vpid);
  VPID_SET_NULL (&load_args->report_last_leaf_vpid);
  load_args->n_keys = 0;
  load_args->report_local_max_key_len = 0;
  load_args->report_n_keys = 0;
  load_args->report_n_oids = 0;
  load_args->report_n_nulls = 0;
  load_args->report_overflow_key_count = 0;
  load_args->report_first_error = NO_ERROR;
  load_args->report_published = false;
  load_args->vacuum_items = NULL;
  load_args->vacuum_count = 0;
  load_args->vacuum_capacity = 0;
  load_args->vacuum_payload_size = 0;
  db_make_null (&load_args->current_key);

  load_args->leaf_nleaf_recdes.data = NULL;
  load_args->ovf_recdes.data = NULL;
  load_args->leaf_nleaf_recdes.data = (char *) os_malloc (src->leaf_nleaf_recdes.area_size);
  load_args->ovf_recdes.data = (char *) os_malloc (src->ovf_recdes.area_size);
  if (load_args->leaf_nleaf_recdes.data == NULL || load_args->ovf_recdes.data == NULL)
    {
      bt_load_free_shard_load_args (thread_p, load_args);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
#if defined (SERVER_MODE)
  assert (load_args->build_mvccid != MVCCID_NULL);
#endif
  *out = load_args;
  return NO_ERROR;
}

void
bt_load_free_shard_load_args (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args)
{
  if (load_args == NULL)
    {
      return;
    }
  pgbuf_unfix_and_init_after_check (thread_p, load_args->leaf.pgptr);
  pgbuf_unfix_and_init_after_check (thread_p, load_args->ovf.pgptr);
  pgbuf_unfix_and_init_after_check (thread_p, load_args->nleaf.pgptr);
  os_free_and_init (load_args->leaf_nleaf_recdes.data);
  os_free_and_init (load_args->ovf_recdes.data);
  pr_clear_value (&load_args->current_key);
  bt_load_clear_vacuum_notifications (load_args);
  free_and_init (load_args);
}

int
bt_load_worker_put_range (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, RECDES * recdes)
{
  return btree_construct_leafs (thread_p, recdes, load_args);
}

int
bt_load_worker_close_shard (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args)
{
  int error;
  if (load_args->leaf.pgptr == NULL)
    {
      return ER_FAILED;
    }
  error = btree_save_last_leafrec (thread_p, load_args);
  if (error != NO_ERROR)
    {
      return error;
    }
  load_args->report_first_leaf_vpid = load_args->vpid_first_leaf;
  load_args->report_last_leaf_vpid = load_args->leaf.vpid;
  load_args->report_n_keys = load_args->n_keys;
  return NO_ERROR;
}

int
bt_load_worker_epilogue (THREAD_ENTRY * thread_p, LOAD_ARGS * load_args, int error)
{
  BT_LOAD_PROVIDER *provider = load_args->provider;
  BT_LOAD_WORKER_SLOT *slot = &provider->slots[load_args->worker_idx];

  pgbuf_unfix_and_init_after_check (thread_p, load_args->leaf.pgptr);
  pgbuf_unfix_and_init_after_check (thread_p, load_args->ovf.pgptr);
  pgbuf_unfix_and_init_after_check (thread_p, load_args->nleaf.pgptr);
  pr_clear_value (&load_args->current_key);
  load_args->report_first_error = error;
  pthread_mutex_lock (&provider->mtx);
  if (error == NO_ERROR)
    {
      slot->state = BT_LOAD_SLOT_DONE;
    }
  else
    {
      slot->state = BT_LOAD_SLOT_ERROR;
      if (provider->first_error == NO_ERROR)
	{
	  provider->first_error = error;
	}
      for (int i = 0; i < provider->n_workers; i++)
	{
	  pthread_cond_broadcast (&provider->cond_worker[i]);
	}
    }
  load_args->report_published = true;
  pthread_cond_signal (&provider->cond_main);
  pthread_mutex_unlock (&provider->mtx);
  return error;
}

int
bt_load_decode_sort_record_key (THREAD_ENTRY * thread_p, const RECDES * recdes, LOAD_ARGS * load_args,
				DB_VALUE * key_out)
{
  S_PARAM_ST sparam;
  RECDES copy = *recdes;
  memset (&sparam, 0, sizeof (sparam));
  db_make_null (&sparam.this_key);
  if (bt_load_get_buf_from_record (&copy, load_args, &sparam, true) != NO_ERROR)
    {
      return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
    }
  *key_out = sparam.this_key;
  return NO_ERROR;
}

static int
bt_load_patch_seam (THREAD_ENTRY * thread_p, LOAD_ARGS * main_load_args, LOAD_ARGS * left, LOAD_ARGS * right)
{
  PAGE_PTR page_left = NULL, page_right = NULL;
  BTREE_NODE_HEADER *header_left, *header_right;
  RECDES record;
  LEAF_REC leaf_record;
  DB_VALUE key_left, key_right;
  bool clear_left, clear_right;
  int offset, key_count, compare, error = ER_FAILED;

  btree_init_temp_key_value (&clear_left, &key_left);
  btree_init_temp_key_value (&clear_right, &key_right);
  page_left = pgbuf_fix (thread_p, &left->report_last_leaf_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (page_left == NULL)
    {
      goto end;
    }
  page_right = pgbuf_fix (thread_p, &right->report_first_leaf_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (page_right == NULL)
    {
      goto end;
    }
  header_left = btree_get_node_header (thread_p, page_left);
  header_right = btree_get_node_header (thread_p, page_right);
  if (header_left == NULL || header_right == NULL)
    {
      goto end;
    }
  key_count = btree_node_number_of_keys (thread_p, page_left);
  if (key_count <= 0 || spage_get_record (thread_p, page_left, key_count, &record, PEEK) != S_SUCCESS)
    {
      goto end;
    }
  error = btree_read_record (thread_p, main_load_args->btid, page_left, &record, &key_left, &leaf_record,
			     BTREE_LEAF_NODE, &clear_left, &offset, PEEK_KEY_VALUE, NULL);
  if (error != NO_ERROR || spage_get_record (thread_p, page_right, 1, &record, PEEK) != S_SUCCESS)
    {
      goto end;
    }
  error = btree_read_record (thread_p, main_load_args->btid, page_right, &record, &key_right, &leaf_record,
			     BTREE_LEAF_NODE, &clear_right, &offset, PEEK_KEY_VALUE, NULL);
  if (error != NO_ERROR)
    {
      goto end;
    }
  compare = btree_compare_key (&key_left, &key_right, main_load_args->btid->key_type, 0, 1, NULL);
  if (compare != DB_LT)
    {
      error = ER_FAILED;
      goto end;
    }
  header_left->next_vpid = right->report_first_leaf_vpid;
  header_right->prev_vpid = left->report_last_leaf_vpid;
  error = btree_log_page (thread_p, &main_load_args->btid->sys_btid->vfid, page_left, true, true);
  page_left = NULL;
  if (error != NO_ERROR)
    {
      goto end;
    }
  error = btree_log_page (thread_p, &main_load_args->btid->sys_btid->vfid, page_right, true, true);
  page_right = NULL;

end:
  btree_clear_key_value (&clear_left, &key_left);
  btree_clear_key_value (&clear_right, &key_right);
  pgbuf_unfix_and_init_after_check (thread_p, page_left);
  pgbuf_unfix_and_init_after_check (thread_p, page_right);
  return error;
}

static int
bt_load_checked_add_report_value (INT64 * total, INT64 value)
{
  if (value < 0 || *total > LLONG_MAX - value)
    {
      return ER_FAILED;
    }
  *total += value;
  return NO_ERROR;
}

int
bt_load_px_join_finalize (THREAD_ENTRY * thread_p, LOAD_ARGS * main_load_args, LOAD_ARGS * shard_load_args[],
			  int n_shards)
{
  BT_LOAD_PROVIDER *provider = shard_load_args[0]->provider;
  int error;
  int n_keys = 0;
  int local_max_key_len = 0;
  INT64 report_n_keys = 0;
  INT64 report_n_oids = 0;
  INT64 report_n_nulls = 0;
  INT64 overflow_key_count = 0;

  for (int i = 0; i < n_shards; i++)
    {
      if (!shard_load_args[i]->report_published || shard_load_args[i]->report_first_error != NO_ERROR)
	{
	  return shard_load_args[i]->report_first_error !=
	    NO_ERROR ? shard_load_args[i]->report_first_error : ER_FAILED;
	}
      if (bt_load_checked_add_report_value (&report_n_keys, shard_load_args[i]->report_n_keys) != NO_ERROR
	  || bt_load_checked_add_report_value (&report_n_oids, shard_load_args[i]->report_n_oids) != NO_ERROR
	  || bt_load_checked_add_report_value (&report_n_nulls, shard_load_args[i]->report_n_nulls) != NO_ERROR
	  || bt_load_checked_add_report_value (&overflow_key_count,
					       shard_load_args[i]->report_overflow_key_count) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      local_max_key_len = MAX (local_max_key_len, shard_load_args[i]->report_local_max_key_len);
      if (bt_load_append_vacuum_notifications (thread_p, shard_load_args[i]) != NO_ERROR)
	{
	  return er_errid ();
	}
      bt_load_clear_vacuum_notifications (shard_load_args[i]);
    }
  for (int i = 1; i < n_shards; i++)
    {
      error = bt_load_patch_seam (thread_p, main_load_args, shard_load_args[i - 1], shard_load_args[i]);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  main_load_args->provider = provider;
  main_load_args->new_page_fn = bt_load_new_page_main_inline;
  main_load_args->vpid_first_leaf = shard_load_args[0]->report_first_leaf_vpid;
  if (report_n_keys > INT_MAX)
    {
      return ER_FAILED;
    }
  n_keys = (int) report_n_keys;
  main_load_args->n_keys = n_keys;
  main_load_args->report_local_max_key_len = local_max_key_len;
  main_load_args->report_n_keys = report_n_keys;
  main_load_args->report_n_oids = report_n_oids;
  main_load_args->report_n_nulls = report_n_nulls;
  main_load_args->report_overflow_key_count = overflow_key_count;

  /*
   * Fully-null keys never enter the final sort run. SORT_ARGS totals, which already include partial-null records,
   * remain authoritative for root/global statistics; report_n_nulls is the checked construct-shard subtotal.
   */
  error = btree_build_nleafs (thread_p, main_load_args, main_load_args->sort_args->n_nulls,
			      main_load_args->sort_args->n_oids, n_keys);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (bt_load_checked_add_report_value (&overflow_key_count,
					main_load_args->report_overflow_key_count - overflow_key_count) != NO_ERROR)
    {
      return ER_FAILED;
    }
  main_load_args->report_overflow_key_count = overflow_key_count;
  return NO_ERROR;
}

#if defined(CUBRID_DEBUG)
/*
 * btree_dump_sort_output () - Sample output function for index sorting
 *   return: NO_ERROR
 *   recdes(in):
 *   load_args(in):
 *
 * Note: This function is a debugging function. It is passed by the
 * btree_index_sort function to the sort_listfile function to print
 * out the contents of the sort items once they are obtained in
 * the requested sorting order.
 *
 */
static int
btree_dump_sort_output (const RECDES * recdes, LOAD_ARGS * load_args)
{
  OID this_oid;
  DB_VALUE this_key;
  OR_BUF buf;
  bool copy = false;
  int key_size = -1;
  int ret = NO_ERROR;

  /* First decompose the input record into the key and oid components */
  or_init (&buf, recdes->data, recdes->length);

  if (or_get_oid (&buf, &this_oid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  buf.ptr = PTR_ALIGN (buf.ptr, MAX_ALIGNMENT);

  /* Do not copy the string--just use the pointer.  The pr_ routines for strings and sets have different semantics for
   * length. */
  if (TP_DOMAIN_TYPE (load_args->btid->key_type) == DB_TYPE_MIDXKEY)
    {
      key_size = buf.endptr - buf.ptr;
    }

  if ((*(load_args->btid->key_type->type->readval)) (&buf, &this_key, load_args->btid->key_type, key_size, copy, NULL,
						     0) != NO_ERROR)
    {
      goto exit_on_error;
    }

  printf ("Attribute: ");
  btree_dump_key (stdout, &this_key);
  printf ("   Volid: %d", this_oid.volid);
  printf ("   Pageid: %d", this_oid.pageid);
  printf ("   Slotid: %d\n", this_oid.slotid);

end:

  copy = btree_clear_key_value (copy, &this_key);

  return ret;

exit_on_error:

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}
#endif /* CUBRID_DEBUG */

/*
 * btree_index_sort () - Sort for the index file creation
 *   return: int
 *   sort_args(in): sort arguments; specifies the sort-attribute as well as
 *	            the structure of the input objects
 *   out_func(in): output function to utilize the sorted items as they are
 *                 produced
 *   out_args(in): arguments to the out_func.
 *
 * Note: This function supports the initial loading phase of B+tree
 * indices by providing an ordered list of (index-attribute
 * value, object address) pairs. It uses the general sorting
 * facility provided in the "sr" module.
 */
static int
btree_index_sort (THREAD_ENTRY * thread_p, SORT_ARGS * sort_args, SORT_PUT_FUNC * out_func, void *out_args)
{
  int i;
  bool includes_tde_class = false;
  TDE_ALGORITHM tde_algo = TDE_ALGORITHM_NONE;

  for (i = 0; i < sort_args->n_classes; i++)
    {
      if (heap_get_class_tde_algorithm (thread_p, &sort_args->class_ids[i], &tde_algo) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      if (tde_algo != TDE_ALGORITHM_NONE)
	{
	  includes_tde_class = true;
	  break;
	}
    }

  return sort_listfile (thread_p, sort_args->hfids[0].vfid.volid, 0 /* TODO - support parallelism */ ,
			&btree_sort_get_next, sort_args, out_func, out_args, compare_driver, sort_args, SORT_DUP,
			NO_SORT_LIMIT, includes_tde_class, SORT_INDEX_LEAF);
}

static SCAN_CODE
get_next_vpid (THREAD_ENTRY * thread_p, ftab_set & ftab, FILE_PARTIAL_SECTOR * fsector, int *offset,
	       VPID * vpid_out, HEAP_SCANCACHE * scan_cache, PGBUF_WATCHER * old_pgwatcher, HFID * hfid)
{
  int error = NO_ERROR;
  assert (*offset >= 0);

  while (true)
    {
      if (fsector == NULL || VSID_IS_NULL (&fsector->vsid))
	{
	  /* try to get new partial sector */
	  FILE_PARTIAL_SECTOR new_fsector = ftab.get_next ();
	  if (VSID_IS_NULL (&new_fsector.vsid))
	    {
	      /* end */
	      if (old_pgwatcher->pgptr != NULL)
		{
		  pgbuf_ordered_unfix (thread_p, old_pgwatcher);
		}
	      return S_END;
	    }
	  *fsector = new_fsector;
	  *offset = 0;
	  vpid_out->volid = new_fsector.vsid.volid;
	  vpid_out->pageid = SECTOR_FIRST_PAGEID (new_fsector.vsid.sectid);
	}
      else
	{
	  (*offset)++;
	  vpid_out->volid = fsector->vsid.volid;
	  vpid_out->pageid = SECTOR_FIRST_PAGEID (fsector->vsid.sectid) + (*offset);
	}
      /* fsector exists */
      for (; *offset < DISK_SECTOR_NPAGES; (*offset)++, (*vpid_out).pageid++)
	{
	  if (vpid_out->pageid == hfid->vfid.fileid && vpid_out->volid == hfid->vfid.volid)
	    {
	      /* ftab page, skip it. */
	      continue;
	    }
	  if (bit64_is_set (fsector->page_bitmap, *offset))
	    {
	      if (scan_cache->page_watcher.pgptr != NULL)
		{
		  pgbuf_replace_watcher (thread_p, &scan_cache->page_watcher, old_pgwatcher);
		}

	      error = pgbuf_ordered_fix (thread_p, vpid_out, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_READ,
					 &scan_cache->page_watcher);
	      er_log_debug (ARG_FILE_LINE, "parallel btree: thread %d fix page %d|%d for scanning", thread_p->index,
			    vpid_out->volid, vpid_out->pageid);

	      if (scan_cache->page_watcher.pgptr == NULL)
		{
		  if (error != NO_ERROR && error != ER_PB_BAD_PAGEID)
		    {
		      /* non-dealloc error (e.g. ER_INTERRUPTED): propagate */
		      if (old_pgwatcher->pgptr != NULL)
			{
			  pgbuf_ordered_unfix (thread_p, old_pgwatcher);
			}
		      return S_ERROR;
		    }
		  /* when bitmap is built, that page was valid.
		   * but now, it's deallocated in some reasons.
		   * this is not error, it can be ignored */
		  er_clear ();
		  continue;
		}

	      if (old_pgwatcher->pgptr != NULL)
		{
		  pgbuf_ordered_unfix (thread_p, old_pgwatcher);
		}

	      if (error != NO_ERROR)
		{
		  return S_ERROR;
		}

	      return S_SUCCESS;
	    }
	}
      assert (fsector);
      VSID_SET_NULL (&fsector->vsid);
    }
  assert (false);
  return S_ERROR;
}

SORT_STATUS
btree_sort_get_next_parallel (THREAD_ENTRY * thread_p, RECDES * temp_recdes, void *arg)
{
  SCAN_CODE page_iter_scan_result, slot_iter_scan_result;
  DB_VALUE dbvalue;
  DB_VALUE *dbvalue_ptr;
  int key_len;
  OID prev_oid;
  SORT_ARGS *sort_args;
  int value_has_null;
  char midxkey_buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_midxkey_buf;
  int *prefix_lengthp;
  int result;
  MVCC_REC_HEADER mvcc_header = MVCC_REC_HEADER_INITIALIZER;
  MVCC_SNAPSHOT mvcc_snapshot_dirty;
  MVCC_SATISFIES_SNAPSHOT_RESULT snapshot_dirty_satisfied;
  bool is_btree_ops_log = prm_get_bool_value (PRM_ID_LOG_BTREE_OPS);

  PGBUF_WATCHER old_pgwatcher;
  HFID hfid;
  VPID vpid = vpid_Null_vpid;

  OID slot_resume_oid;

  db_make_null (&dbvalue);

  aligned_midxkey_buf = PTR_ALIGN (midxkey_buf, MAX_ALIGNMENT);

  sort_args = (SORT_ARGS *) arg;
  prev_oid = sort_args->cur_oid;

  mvcc_snapshot_dirty.snapshot_fnc = mvcc_satisfies_dirty;

  VPID_SET_NULL (&vpid);
  hfid = sort_args->hfids[0];
  PGBUF_INIT_WATCHER (&old_pgwatcher, PGBUF_ORDERED_HEAP_NORMAL, &hfid);

  OID_SET_NULL (&slot_resume_oid);

  if (OID_ISNULL (&sort_args->cur_oid))
    {
      page_iter_scan_result =
	get_next_vpid (thread_p, (*sort_args->ftab_sets)[sort_args->cur_class], &sort_args->curr_sec,
		       &sort_args->curr_pgoffset, &vpid, &sort_args->hfscan_cache, &old_pgwatcher,
		       &sort_args->hfids[sort_args->cur_class]);
      if (page_iter_scan_result == S_END)
	{
	  return SORT_NOMORE_RECS;
	}
      else if (page_iter_scan_result == S_SUCCESS)
	{
	  ;
	}
      else
	{
	  return SORT_ERROR_OCCURRED;
	}
    }
  else
    {
      vpid.pageid = sort_args->cur_oid.pageid;
      vpid.volid = sort_args->cur_oid.volid;
    }

  do
    {				/* Infinite loop */
      int cur_class, attr_offset;
      bool save_cache_last_fix_page;

      /*
       * This infinite loop will be exited when a satisfactory next value is
       * found (i.e., when an object belonging to this class with a non-null
       * attribute value is found), or when there are no more objects in the
       * heap files.
       */

      /*
       * RETRIEVE THE NEXT OBJECT
       */

      cur_class = sort_args->cur_class;
      attr_offset = cur_class * sort_args->n_attrs;
      sort_args->in_recdes.data = NULL;
      if (vpid.pageid == sort_args->hfids[cur_class].vfid.fileid
	  && vpid.volid == sort_args->hfids[cur_class].vfid.volid)
	{
	  /* ftab page */
	  slot_iter_scan_result = S_END;
	}
      else
	{
	  slot_resume_oid = sort_args->cur_oid;
	  slot_iter_scan_result =
	    heap_next_1page (thread_p, &sort_args->hfids[cur_class], &vpid, &sort_args->class_ids[cur_class],
			     &sort_args->cur_oid, &sort_args->in_recdes, &sort_args->hfscan_cache,
			     sort_args->hfscan_cache.cache_last_fix_page ? PEEK : COPY);
	}

      switch (slot_iter_scan_result)
	{
	case S_SUCCESS:
	  break;

	case S_END:
	  {
	    page_iter_scan_result =
	      get_next_vpid (thread_p, (*sort_args->ftab_sets)[sort_args->cur_class], &sort_args->curr_sec,
			     &sort_args->curr_pgoffset, &vpid, &sort_args->hfscan_cache, &old_pgwatcher,
			     &sort_args->hfids[sort_args->cur_class]);
	    if (page_iter_scan_result == S_END)
	      {
		/* fall through */
	      }
	    else if (page_iter_scan_result == S_SUCCESS)
	      {
		continue;
	      }
	    else
	      {
		return SORT_ERROR_OCCURRED;
	      }

	    /* No more objects in this heap, finish the current scan */
	    save_cache_last_fix_page = sort_args->hfscan_cache.cache_last_fix_page;
	    bt_load_heap_scancache_end_for_attrinfo (thread_p, sort_args, NULL, NULL);

	    /* Are we through with all the non-null heaps? */
	    sort_args->cur_class++;
	    while ((sort_args->cur_class < sort_args->n_classes)
		   && HFID_IS_NULL (&sort_args->hfids[sort_args->cur_class]))
	      {
		sort_args->cur_class++;
	      }

	    if (sort_args->cur_class == sort_args->n_classes)
	      {
		return SORT_NOMORE_RECS;
	      }
	    else
	      {
		/* When there is a class inheritance relationship, n_classes may be 2 or more 
		 * only if it is a reverse unique, unique, or primary index.
		 * In addition, filter and func_index_info cannot exist in this case.   
		 */
		/* start up the next scan */
		cur_class = sort_args->cur_class;
		attr_offset = cur_class * sort_args->n_attrs;

		if (bt_load_heap_scancache_start_for_attrinfo
		    (thread_p, sort_args, NULL, NULL, save_cache_last_fix_page) != NO_ERROR)
		  {
		    return SORT_ERROR_OCCURRED;
		  }

		/* set the scan to the initial state for this new heap */
		OID_SET_NULL (&sort_args->cur_oid);

		if (is_btree_ops_log)
		  {
		    _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: load start on class(%d, %d, %d), btid(%d, (%d, %d)).",
				   sort_args->class_ids[sort_args->cur_class].volid,
				   sort_args->class_ids[sort_args->cur_class].pageid,
				   sort_args->class_ids[sort_args->cur_class].slotid,
				   sort_args->btid->sys_btid->root_pageid, sort_args->btid->sys_btid->vfid.volid,
				   sort_args->btid->sys_btid->vfid.fileid);
		  }
		hfid = sort_args->hfids[sort_args->cur_class];
		PGBUF_INIT_WATCHER (&old_pgwatcher, PGBUF_ORDERED_HEAP_NORMAL, &hfid);
	      }
	    continue;
	  }
	  /*
	     case S_ERROR:
	     case S_DOESNT_EXIST:
	     case S_DOESNT_FIT:
	     case S_SUCCESS_CHN_UPTODATE:
	     case S_SNAPSHOT_NOT_SATISFIED:
	   */
	default:
	  return SORT_ERROR_OCCURRED;
	}

      /*
       * Produce the sort item for this object
       */

      /* filter out dead records before any more checks */
      if (or_mvcc_get_header (&sort_args->in_recdes, &mvcc_header) != NO_ERROR)
	{
	  return SORT_ERROR_OCCURRED;
	}
      if (MVCC_IS_HEADER_DELID_VALID (&mvcc_header) && MVCC_GET_DELID (&mvcc_header) < sort_args->oldest_visible_mvccid)
	{
	  continue;
	}
      if (MVCC_IS_HEADER_INSID_NOT_ALL_VISIBLE (&mvcc_header)
	  && MVCC_GET_INSID (&mvcc_header) < sort_args->oldest_visible_mvccid)
	{
	  /* Insert MVCCID is now visible to everyone. Clear it to avoid unnecessary vacuuming. */
	  MVCC_CLEAR_FLAG_BITS (&mvcc_header, OR_MVCC_FLAG_VALID_INSID);
	}

      snapshot_dirty_satisfied = mvcc_snapshot_dirty.snapshot_fnc (thread_p, &mvcc_header, &mvcc_snapshot_dirty);

      if (sort_args->filter)
	{
	  if (heap_attrinfo_read_dbvalues
	      (thread_p, &sort_args->cur_oid, &sort_args->in_recdes, sort_args->filter->cache_pred) != NO_ERROR)
	    {
	      return SORT_ERROR_OCCURRED;
	    }

	  result = (*sort_args->filter_eval_func) (thread_p, sort_args->filter->pred, NULL, &sort_args->cur_oid);
	  if (result == V_ERROR)
	    {
	      return SORT_ERROR_OCCURRED;
	    }
	  else if (result != V_TRUE)
	    {
	      continue;
	    }
	}

      if (sort_args->func_index_info && sort_args->func_index_info->expr)
	{
	  if (snapshot_dirty_satisfied != SNAPSHOT_SATISFIED)
	    {
	      /* Check snapshot before key generation. Key generation may leads to errors when a function is involved. */
	      continue;
	    }
	}

      prefix_lengthp = (sort_args->attrs_prefix_length) ? &(sort_args->attrs_prefix_length[0]) : NULL;
      dbvalue_ptr =
	heap_attrinfo_generate_key (thread_p, sort_args->n_attrs, &sort_args->attr_ids[attr_offset], prefix_lengthp,
				    &sort_args->attr_info, &sort_args->in_recdes, &dbvalue, aligned_midxkey_buf,
				    sort_args->func_index_info, NULL, &sort_args->cur_oid);
      if (dbvalue_ptr == NULL)
	{
	  return SORT_ERROR_OCCURRED;
	}

      value_has_null = 0;	/* init */
      if (DB_IS_NULL (dbvalue_ptr) || btree_multicol_key_has_null (dbvalue_ptr))
	{
	  if (sort_args->not_null_flag && snapshot_dirty_satisfied == SNAPSHOT_SATISFIED)
	    {
	      if (dbvalue_ptr == &dbvalue || dbvalue_ptr->need_clear == true)
		{
		  pr_clear_value (dbvalue_ptr);
		}

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NOT_NULL_DOES_NOT_ALLOW_NULL_VALUE, 0);
	      return SORT_ERROR_OCCURRED;
	    }

	  value_has_null = 1;	/* found null columns */
	}

      if (DB_IS_NULL (dbvalue_ptr) || btree_multicol_key_is_null (dbvalue_ptr))
	{
	  if (snapshot_dirty_satisfied == SNAPSHOT_SATISFIED)
	    {
	      /* All objects that were not candidates for vacuum are loaded, but statistics should only care for
	       * objects that have not been deleted and committed at the time of load. */
	      sort_args->n_oids++;	/* Increment the OID counter */
	      sort_args->n_nulls++;	/* Increment the NULL counter */
	    }
	  if (dbvalue_ptr == &dbvalue || dbvalue_ptr->need_clear == true)
	    {
	      pr_clear_value (dbvalue_ptr);
	    }
	  if (is_btree_ops_log)
	    {
	      _er_log_debug (ARG_FILE_LINE,
			     "DEBUG_BTREE: load sort found null at oid(%d, %d, %d)"
			     ", class_oid(%d, %d, %d), btid(%d, (%d, %d).", sort_args->cur_oid.volid,
			     sort_args->cur_oid.pageid, sort_args->cur_oid.slotid,
			     sort_args->class_ids[sort_args->cur_class].volid,
			     sort_args->class_ids[sort_args->cur_class].pageid,
			     sort_args->class_ids[sort_args->cur_class].slotid, sort_args->btid->sys_btid->root_pageid,
			     sort_args->btid->sys_btid->vfid.volid, sort_args->btid->sys_btid->vfid.fileid);
	    }
	  continue;
	}

      key_len = btree_get_disk_size_of_key (dbvalue_ptr);
      if (key_len > 0)
	{
	  result = bt_load_put_buf_to_record (temp_recdes, sort_args, value_has_null, &prev_oid, &mvcc_header,
					      dbvalue_ptr, key_len, cur_class, is_btree_ops_log);
	  if (result != NO_ERROR)
	    {
	      goto nofit;
	    }
	  if (key_len >= BTREE_MAX_KEYLEN_INPAGE)
	    {
	      int remaining = key_len - (DB_PAGESIZE - (int) offsetof (OVERFLOW_FIRST_PART, data));
	      int pages = 1;
	      if (remaining > 0)
		{
		  pages += CEIL_PTVDIV (remaining, DB_PAGESIZE - (int) offsetof (OVERFLOW_REST_PART, data));
		}
	      sort_args->n_ovf_keys++;
	      sort_args->sum_ovf_pages += pages;
	    }

	  if (dbvalue_ptr == &dbvalue || dbvalue_ptr->need_clear == true)
	    {
	      pr_clear_value (dbvalue_ptr);
	    }
	}

      if (snapshot_dirty_satisfied == SNAPSHOT_SATISFIED)
	{
	  /* All objects that were not candidates for vacuum are loaded, but statistics should only care for objects
	   * that have not been deleted and committed at the time of load. */
	  sort_args->n_oids++;	/* Increment the OID counter */
	}

      if (key_len > 0)
	{
	  return SORT_SUCCESS;
	}
    }
  while (true);

nofit:

  assert (!VPID_ISNULL (&vpid));
  if (!OID_ISNULL (&slot_resume_oid))
    {
      sort_args->cur_oid = slot_resume_oid;
    }
  else
    {
      sort_args->cur_oid.volid = vpid.volid;
      sort_args->cur_oid.pageid = vpid.pageid;
      sort_args->cur_oid.slotid = 0;
    }

  if (dbvalue_ptr == &dbvalue || dbvalue_ptr->need_clear == true)
    {
      pr_clear_value (dbvalue_ptr);
    }

  return SORT_REC_DOESNT_FIT;
}


/*
 * btree_sort_get_next () - Get_key function for index sorting
 *   return: SORT_STATUS
 *   temp_recdes(in): temporary record descriptor; specifies where to put the
 *                    next sort item.
 *   arg(in): sort arguments; provides information about how to produce
 *            the next sort item.
 *
 * Note: This function is passed by the "btree_index_sort" function to
 * the "sort_listfile" function to obtain the value of the attribute
 * (on which the B+tree index for the class is to be created)
 * of each object successively.
 */
static SORT_STATUS
btree_sort_get_next (THREAD_ENTRY * thread_p, RECDES * temp_recdes, void *arg)
{
  SCAN_CODE scan_result;
  DB_VALUE dbvalue;
  DB_VALUE *dbvalue_ptr;
  int key_len;
  OID prev_oid;
  SORT_ARGS *sort_args;
  int value_has_null;
  char midxkey_buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_midxkey_buf;
  int *prefix_lengthp;
  int result;
  MVCC_REC_HEADER mvcc_header = MVCC_REC_HEADER_INITIALIZER;
  MVCC_SNAPSHOT mvcc_snapshot_dirty;
  MVCC_SATISFIES_SNAPSHOT_RESULT snapshot_dirty_satisfied;
  bool is_btree_ops_log = prm_get_bool_value (PRM_ID_LOG_BTREE_OPS);

  db_make_null (&dbvalue);

  aligned_midxkey_buf = PTR_ALIGN (midxkey_buf, MAX_ALIGNMENT);

  sort_args = (SORT_ARGS *) arg;
  prev_oid = sort_args->cur_oid;

  mvcc_snapshot_dirty.snapshot_fnc = mvcc_satisfies_dirty;

  do
    {				/* Infinite loop */
      int cur_class, attr_offset;
      bool save_cache_last_fix_page;

      /*
       * This infinite loop will be exited when a satisfactory next value is
       * found (i.e., when an object belonging to this class with a non-null
       * attribute value is found), or when there are no more objects in the
       * heap files.
       */

      /*
       * RETRIEVE THE NEXT OBJECT
       */

      cur_class = sort_args->cur_class;
      attr_offset = cur_class * sort_args->n_attrs;
      sort_args->in_recdes.data = NULL;
      scan_result =
	heap_next (thread_p, &sort_args->hfids[cur_class], &sort_args->class_ids[cur_class], &sort_args->cur_oid,
		   &sort_args->in_recdes, &sort_args->hfscan_cache,
		   sort_args->hfscan_cache.cache_last_fix_page ? PEEK : COPY);

      switch (scan_result)
	{
	case S_SUCCESS:
	  break;

	case S_END:
	  /* No more objects in this heap, finish the current scan */
	  save_cache_last_fix_page = sort_args->hfscan_cache.cache_last_fix_page;
	  bt_load_heap_scancache_end_for_attrinfo (thread_p, sort_args, NULL, NULL);

	  /* Are we through with all the non-null heaps? */
	  sort_args->cur_class++;
	  while ((sort_args->cur_class < sort_args->n_classes)
		 && HFID_IS_NULL (&sort_args->hfids[sort_args->cur_class]))
	    {
	      sort_args->cur_class++;
	    }

	  if (sort_args->cur_class == sort_args->n_classes)
	    {
	      return SORT_NOMORE_RECS;
	    }
	  else
	    {
	      /* When there is a class inheritance relationship, n_classes may be 2 or more 
	       * only if it is a reverse unique, unique, or primary index.
	       * In addition, filter and func_index_info cannot exist in this case.   
	       */
	      /* start up the next scan */
	      cur_class = sort_args->cur_class;
	      attr_offset = cur_class * sort_args->n_attrs;

	      if (bt_load_heap_scancache_start_for_attrinfo (thread_p, sort_args, NULL, NULL, save_cache_last_fix_page)
		  != NO_ERROR)
		{
		  return SORT_ERROR_OCCURRED;
		}

	      /* set the scan to the initial state for this new heap */
	      OID_SET_NULL (&sort_args->cur_oid);

	      if (is_btree_ops_log)
		{
		  _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: load start on class(%d, %d, %d), btid(%d, (%d, %d)).",
				 sort_args->class_ids[sort_args->cur_class].volid,
				 sort_args->class_ids[sort_args->cur_class].pageid,
				 sort_args->class_ids[sort_args->cur_class].slotid,
				 sort_args->btid->sys_btid->root_pageid, sort_args->btid->sys_btid->vfid.volid,
				 sort_args->btid->sys_btid->vfid.fileid);
		}
	    }
	  continue;

	  /*
	     case S_ERROR:
	     case S_DOESNT_EXIST:
	     case S_DOESNT_FIT:
	     case S_SUCCESS_CHN_UPTODATE:
	     case S_SNAPSHOT_NOT_SATISFIED:
	   */
	default:
	  return SORT_ERROR_OCCURRED;
	}

      /*
       * Produce the sort item for this object
       */

      /* filter out dead records before any more checks */
      if (or_mvcc_get_header (&sort_args->in_recdes, &mvcc_header) != NO_ERROR)
	{
	  return SORT_ERROR_OCCURRED;
	}
      if (MVCC_IS_HEADER_DELID_VALID (&mvcc_header) && MVCC_GET_DELID (&mvcc_header) < sort_args->oldest_visible_mvccid)
	{
	  continue;
	}
      if (MVCC_IS_HEADER_INSID_NOT_ALL_VISIBLE (&mvcc_header)
	  && MVCC_GET_INSID (&mvcc_header) < sort_args->oldest_visible_mvccid)
	{
	  /* Insert MVCCID is now visible to everyone. Clear it to avoid unnecessary vacuuming. */
	  MVCC_CLEAR_FLAG_BITS (&mvcc_header, OR_MVCC_FLAG_VALID_INSID);
	}

      snapshot_dirty_satisfied = mvcc_snapshot_dirty.snapshot_fnc (thread_p, &mvcc_header, &mvcc_snapshot_dirty);

      if (sort_args->filter)
	{
	  if (heap_attrinfo_read_dbvalues
	      (thread_p, &sort_args->cur_oid, &sort_args->in_recdes, sort_args->filter->cache_pred) != NO_ERROR)
	    {
	      return SORT_ERROR_OCCURRED;
	    }

	  result = (*sort_args->filter_eval_func) (thread_p, sort_args->filter->pred, NULL, &sort_args->cur_oid);
	  if (result == V_ERROR)
	    {
	      return SORT_ERROR_OCCURRED;
	    }
	  else if (result != V_TRUE)
	    {
	      continue;
	    }
	}

      if (sort_args->func_index_info && sort_args->func_index_info->expr)
	{
	  if (snapshot_dirty_satisfied != SNAPSHOT_SATISFIED)
	    {
	      /* Check snapshot before key generation. Key generation may leads to errors when a function is involved. */
	      continue;
	    }
	}

      prefix_lengthp = (sort_args->attrs_prefix_length) ? &(sort_args->attrs_prefix_length[0]) : NULL;
      dbvalue_ptr =
	heap_attrinfo_generate_key (thread_p, sort_args->n_attrs, &sort_args->attr_ids[attr_offset], prefix_lengthp,
				    &sort_args->attr_info, &sort_args->in_recdes, &dbvalue, aligned_midxkey_buf,
				    sort_args->func_index_info, NULL, &sort_args->cur_oid);
      if (dbvalue_ptr == NULL)
	{
	  return SORT_ERROR_OCCURRED;
	}

      value_has_null = 0;	/* init */
      if (DB_IS_NULL (dbvalue_ptr) || btree_multicol_key_has_null (dbvalue_ptr))
	{
	  if (sort_args->not_null_flag && snapshot_dirty_satisfied == SNAPSHOT_SATISFIED)
	    {
	      if (dbvalue_ptr == &dbvalue || dbvalue_ptr->need_clear == true)
		{
		  pr_clear_value (dbvalue_ptr);
		}

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NOT_NULL_DOES_NOT_ALLOW_NULL_VALUE, 0);
	      return SORT_ERROR_OCCURRED;
	    }

	  value_has_null = 1;	/* found null columns */
	}

      if (DB_IS_NULL (dbvalue_ptr) || btree_multicol_key_is_null (dbvalue_ptr))
	{
	  if (snapshot_dirty_satisfied == SNAPSHOT_SATISFIED)
	    {
	      /* All objects that were not candidates for vacuum are loaded, but statistics should only care for
	       * objects that have not been deleted and committed at the time of load. */
	      sort_args->n_oids++;	/* Increment the OID counter */
	      sort_args->n_nulls++;	/* Increment the NULL counter */
	    }
	  if (dbvalue_ptr == &dbvalue || dbvalue_ptr->need_clear == true)
	    {
	      pr_clear_value (dbvalue_ptr);
	    }
	  if (is_btree_ops_log)
	    {
	      _er_log_debug (ARG_FILE_LINE,
			     "DEBUG_BTREE: load sort found null at oid(%d, %d, %d)"
			     ", class_oid(%d, %d, %d), btid(%d, (%d, %d).", sort_args->cur_oid.volid,
			     sort_args->cur_oid.pageid, sort_args->cur_oid.slotid,
			     sort_args->class_ids[sort_args->cur_class].volid,
			     sort_args->class_ids[sort_args->cur_class].pageid,
			     sort_args->class_ids[sort_args->cur_class].slotid, sort_args->btid->sys_btid->root_pageid,
			     sort_args->btid->sys_btid->vfid.volid, sort_args->btid->sys_btid->vfid.fileid);
	    }
	  continue;
	}

      key_len = sort_args->key_type->type->get_disk_size_of_value (dbvalue_ptr);
      if (key_len > 0)
	{
	  result = bt_load_put_buf_to_record (temp_recdes, sort_args, value_has_null, &prev_oid, &mvcc_header,
					      dbvalue_ptr, key_len, cur_class, is_btree_ops_log);
	  if (result != NO_ERROR)
	    {
	      goto nofit;
	    }
	  if (key_len >= BTREE_MAX_KEYLEN_INPAGE)
	    {
	      int remaining = key_len - (DB_PAGESIZE - (int) offsetof (OVERFLOW_FIRST_PART, data));
	      int pages = 1;
	      if (remaining > 0)
		{
		  pages += CEIL_PTVDIV (remaining, DB_PAGESIZE - (int) offsetof (OVERFLOW_REST_PART, data));
		}
	      sort_args->n_ovf_keys++;
	      sort_args->sum_ovf_pages += pages;
	    }

	  if (dbvalue_ptr == &dbvalue || dbvalue_ptr->need_clear == true)
	    {
	      pr_clear_value (dbvalue_ptr);
	    }
	}

      if (snapshot_dirty_satisfied == SNAPSHOT_SATISFIED)
	{
	  /* All objects that were not candidates for vacuum are loaded, but statistics should only care for objects
	   * that have not been deleted and committed at the time of load. */
	  sort_args->n_oids++;	/* Increment the OID counter */
	}

      if (key_len > 0)
	{
	  return SORT_SUCCESS;
	}
    }
  while (true);

nofit:

  if (dbvalue_ptr == &dbvalue || dbvalue_ptr->need_clear == true)
    {
      pr_clear_value (dbvalue_ptr);
    }

  return SORT_REC_DOESNT_FIT;
}

/*
 * compare_driver () -
 *   return:
 *   first(in):
 *   second(in):
 *   arg(in):
 */
static int
compare_driver (const void *first, const void *second, void *arg)
{
  char *mem1 = *(char **) first;
  char *mem2 = *(char **) second;
  int has_null;
  SORT_ARGS *sort_args;
  TP_DOMAIN *key_type;
  char *oidptr1, *oidptr2;
  int c = DB_UNK;

  sort_args = (SORT_ARGS *) arg;
  key_type = sort_args->key_type;

  assert (PTR_ALIGN (mem1, MAX_ALIGNMENT) == mem1);
  assert (PTR_ALIGN (mem2, MAX_ALIGNMENT) == mem2);

  /* Skip next link */
  mem1 += sizeof (char *);
  mem2 += sizeof (char *);

  /* Read value_has_null */
  assert (OR_GET_BYTE (mem1) == 0 || OR_GET_BYTE (mem1) == 1);
  assert (OR_GET_BYTE (mem2) == 0 || OR_GET_BYTE (mem2) == 1);
  has_null = (OR_GET_BYTE (mem1) || OR_GET_BYTE (mem2)) ? 1 : 0;

  mem1 += OR_INT_SIZE;
  mem2 += OR_INT_SIZE;

  assert (PTR_ALIGN (mem1, INT_ALIGNMENT) == mem1);
  assert (PTR_ALIGN (mem2, INT_ALIGNMENT) == mem2);

  oidptr1 = mem1;
  oidptr2 = mem2;

  /* Skip the oids */
  if (BTREE_IS_UNIQUE (sort_args->unique_pk))
    {				/* unique index */
      mem1 += (2 * OR_OID_SIZE);
      mem2 += (2 * OR_OID_SIZE);
    }
  else
    {				/* non-unique index */
      mem1 += OR_OID_SIZE;
      mem2 += OR_OID_SIZE;
    }

  assert (PTR_ALIGN (mem1, INT_ALIGNMENT) == mem1);
  assert (PTR_ALIGN (mem2, INT_ALIGNMENT) == mem2);

  /* Skip the MVCCID's */
  mem1 += 2 * OR_MVCCID_SIZE;
  mem2 += 2 * OR_MVCCID_SIZE;

  assert (PTR_ALIGN (mem1, INT_ALIGNMENT) == mem1);
  assert (PTR_ALIGN (mem2, INT_ALIGNMENT) == mem2);

  if (TP_DOMAIN_TYPE (key_type) == DB_TYPE_MIDXKEY)
    {
      int i;
      char *nullmap_ptr1, *nullmap_ptr2;
      int header_size;
      TP_DOMAIN *dom;

      /* fast implementation of pr_midxkey_compare (). do not use DB_VALUE container for speed-up */

      nullmap_ptr1 = mem1;
      nullmap_ptr2 = mem2;

      header_size = or_multi_header_size (key_type->precision);

      mem1 += header_size;
      mem2 += header_size;

#if !defined(NDEBUG)
      for (i = 0, dom = key_type->setdomain; dom; dom = dom->next, i++);
      assert (i == key_type->precision);

      if (sort_args->func_index_info != NULL)
	{
	  /* In the following cases, the precision may be smaller than n_attrs.  
	   * create index idx on tbl(left(s2, v1),v3); 
	   * So, remove the assert().  */
	  //assert (sort_args->n_attrs <= key_type->precision);
	}
      else
	{
	  assert (sort_args->n_attrs == key_type->precision);
	}
      assert (key_type->setdomain != NULL);
#endif

      for (i = 0, dom = key_type->setdomain; i < key_type->precision && dom; i++, dom = dom->next)
	{
	  /* val1 or val2 is NULL */
	  if (has_null)
	    {
	      if (or_multi_is_null (nullmap_ptr1, i))
		{		/* element val is null? */
		  if (or_multi_is_null (nullmap_ptr2, i))
		    {
		      continue;
		    }

		  c = DB_LT;
		  break;	/* exit for-loop */
		}
	      else if (or_multi_is_null (nullmap_ptr2, i))
		{
		  c = DB_GT;
		  break;	/* exit for-loop */
		}
	    }

	  /* check for val1 and val2 same domain */
	  c = dom->type->index_cmpdisk (mem1, mem2, dom, 0, 1, NULL);
	  assert (c == DB_LT || c == DB_EQ || c == DB_GT);

	  if (c != DB_EQ)
	    {
	      break;		/* exit for-loop */
	    }

	  mem1 += pr_midxkey_element_disk_size (mem1, dom);
	  mem2 += pr_midxkey_element_disk_size (mem2, dom);
	}			/* for (i = 0; ... ) */
      assert (c == DB_LT || c == DB_EQ || c == DB_GT);

      if (dom && dom->is_desc)
	{
	  c = ((c == DB_GT) ? DB_LT : (c == DB_LT) ? DB_GT : c);
	}
    }
  else
    {
      OR_BUF buf_val1, buf_val2;
      DB_VALUE val1, val2;

      or_init (&buf_val1, mem1, -1);
      or_init (&buf_val2, mem2, -1);

      if (key_type->type->data_readval (&buf_val1, &val1, key_type, -1, false, NULL, 0) != NO_ERROR)
	{
	  assert (false);
	  return DB_UNK;
	}

      if (key_type->type->data_readval (&buf_val2, &val2, key_type, -1, false, NULL, 0) != NO_ERROR)
	{
	  assert (false);
	  return DB_UNK;
	}

      c = btree_compare_key (&val1, &val2, key_type, 0, 1, NULL);

      /* Clear the values if it is required */
      if (DB_NEED_CLEAR (&val1))
	{
	  pr_clear_value (&val1);
	}

      if (DB_NEED_CLEAR (&val2))
	{
	  pr_clear_value (&val2);
	}
    }

  assert (c == DB_LT || c == DB_EQ || c == DB_GT);

  /* compare OID for non-unique index */
  if (c == DB_EQ)
    {
      OID first_oid, second_oid;

      OR_GET_OID (oidptr1, &first_oid);
      OR_GET_OID (oidptr2, &second_oid);

      assert_release (!OID_EQ (&first_oid, &second_oid));
      return (OID_LT (&first_oid, &second_oid) ? DB_LT : DB_GT);
    }

  return c;
}

/*
 * Linked list implementation
 */

/*
 * list_add () -
 *   return: NO_ERROR
 *   list(in): which list to add
 *   pageid(in): what value to put to the new node
 *
 * Note: This function adds a new node to the end of the given list.
 */
static int
list_add (BTREE_NODE ** list, VPID * pageid)
{
  BTREE_NODE *new_node;
  BTREE_NODE *next_node;
  int ret = NO_ERROR;

  new_node = (BTREE_NODE *) os_malloc (sizeof (BTREE_NODE));
  if (new_node == NULL)
    {
      goto exit_on_error;
    }

  new_node->pageid = *pageid;
  new_node->next = NULL;

  if (*list == NULL)
    {
      *list = new_node;
    }
  else
    {
      next_node = *list;
      while (next_node->next != NULL)
	{
	  next_node = next_node->next;
	}

      next_node->next = new_node;
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * list_remove_first () -
 *   return: error code
 *   list(in):
 *
 * Note: This function removes the first node of the given list (if it has one).
 */
static void
list_remove_first (BTREE_NODE ** list)
{
  BTREE_NODE *temp;

  if (*list != NULL)
    {
      temp = *list;
      *list = (*list)->next;
      os_free_and_init (temp);
    }
}

/*
 * list_clear () -
 *   return: error code
 *   list(in):
 */
static void
list_clear (BTREE_NODE * list)
{
  BTREE_NODE *p, *next;

  for (p = list; p != NULL; p = next)
    {
      next = p->next;

      os_free_and_init (p);
    }
}

/*
 * list_length () -
 *   return: int
 *   this_list(in): which list
 *
 * Note: This function returns the number of elements kept in the
 * given linked list.
 */
static int
list_length (const BTREE_NODE * this_list)
{
  int length = 0;

  while (this_list != NULL)
    {
      length++;
      this_list = this_list->next;
    }

  return length;
}

#if defined(CUBRID_DEBUG)
/*
 * list_print () -
 *   return: this_list
 *   this_list(in): which list to print
 *
 * Note: This function prints the elements of the given linked list;
 * It is used for debugging purposes.
 */
static void
list_print (const BTREE_NODE * this_list)
{
  while (this_list != NULL)
    {
      (void) printf ("{%d, %d}n", this_list->pageid.volid, this_list->pageid.pageid);
      this_list = this_list->next;
    }
}


/*
 * btree_load_foo_debug () -
 *   return:
 *
 * Note: To avoid warning during development
 */
void
btree_load_foo_debug (void)
{
  (void) btree_dump_sort_output (NULL, NULL);
  list_print (NULL);
}
#endif /* CUBRID_DEBUG */

/*
 * btree_rv_nodehdr_dump () - Dump node header recovery information
 *   return: int
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump node header recovery information
 */
void
btree_rv_nodehdr_dump (FILE * fp, int length, void *data)
{
  BTREE_NODE_HEADER *header = NULL;

  header = (BTREE_NODE_HEADER *) data;
  assert (header != NULL);

  fprintf (fp, "\nNODE_TYPE: %s MAX_KEY_LEN: %4d PREV_PAGEID: {%4d , %4d} NEXT_PAGEID: {%4d , %4d} \n\n",
	   header->node_level > 1 ? "NON_LEAF" : "LEAF", header->max_key_len, header->prev_vpid.volid,
	   header->prev_vpid.pageid, header->next_vpid.volid, header->next_vpid.pageid);
}

/*
 * btree_node_number_of_keys () -
 *   return: int
 *
 */
int
btree_node_number_of_keys (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr)
{
  int key_cnt;

  assert (page_ptr != NULL);
#if !defined(NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, page_ptr, PAGE_BTREE);
#endif

  key_cnt = spage_number_of_records (page_ptr) - 1;

#if !defined(NDEBUG)
  {
    BTREE_NODE_HEADER *header = NULL;
    BTREE_NODE_TYPE node_type;

    header = btree_get_node_header (thread_p, page_ptr);
    if (header == NULL)
      {
	assert (false);
      }

    node_type = (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

    if ((node_type == BTREE_NON_LEAF_NODE && key_cnt <= 0) || (node_type == BTREE_LEAF_NODE && key_cnt < 0))
      {
	er_log_debug (ARG_FILE_LINE, "btree_node_number_of_keys: node key count underflow: %d\n", key_cnt);
	assert (false);
      }
  }
#endif

  assert_release (key_cnt >= 0);

  return key_cnt;
}

/*
 * btree_load_check_fk () - Checks if the current foreign key that needs to be loaded is passing all the requirements.
 *
 *   return: NO_ERROR or error code.
 *   load_args(in): Context for the loaded index (Includes leaf level of the foreign key)
 *   sort_args(in): Context for sorting (Includes info on primary key)
 *
 */
int
btree_load_check_fk (THREAD_ENTRY * thread_p, const LOAD_ARGS * load_args, const SORT_ARGS * sort_args)
{
  DB_VALUE fk_key, pk_key;
  bool clear_fk_key, clear_pk_key;
  int fk_node_key_cnt = -1, pk_node_key_cnt = -1;
  BTREE_NODE_HEADER *fk_node_header = NULL, *pk_node_header = NULL;
  VPID vpid;
  int ret = NO_ERROR, i;
  PAGE_PTR curr_fk_pageptr = NULL, old_page = NULL;
  INDX_SCAN_ID pk_isid;
  BTREE_SCAN pk_bt_scan;
  INT16 fk_slot_id = -1;
  bool found = false, pk_has_slot_visible = false, fk_has_visible = false;
  char *val_print = NULL;
  bool is_fk_scan_desc = false;
  MVCC_SNAPSHOT mvcc_snapshot_dirty;
  int lock_ret = LK_GRANTED;
  DB_VALUE_COMPARE_RESULT compare_ret;
  OR_CLASSREP *classrepr = NULL;
  int classrepr_cacheindex = -1, part_count = -1, pos = -1;
  bool clear_pcontext = false;
  PRUNING_CONTEXT pcontext;
  BTID pk_btid;
  OID pk_clsoid;
  HFID pk_dummy_hfid;
  BTREE_SCAN_PART *partitions = NULL;
  bool has_nulls = false;

  bool has_deduplicate_key_col = false;
  DB_VALUE new_fk_key[2];
  DB_VALUE *fk_key_ptr = &fk_key;

  db_make_null (&(new_fk_key[0]));
  db_make_null (&(new_fk_key[1]));
  if (sort_args->n_attrs > 1)
    {
      // We cannot make a PK with a function. Therefore, only the last member is checked.  
      has_deduplicate_key_col = IS_DEDUPLICATE_KEY_ATTR_ID (sort_args->attr_ids[sort_args->n_attrs - 1]);
      if (has_deduplicate_key_col)
	{
	  fk_key_ptr = &(new_fk_key[0]);
	}
    }

  btree_init_temp_key_value (&clear_fk_key, &fk_key);
  btree_init_temp_key_value (&clear_pk_key, &pk_key);

  mvcc_snapshot_dirty.snapshot_fnc = mvcc_satisfies_dirty;

  /* Initialize index scan on primary key btid. */
  scan_init_index_scan (&pk_isid, NULL, NULL);
  BTREE_INIT_SCAN (&pk_bt_scan);

  /* Lock the primary key class. */
  lock_ret = lock_object (thread_p, sort_args->fk_refcls_oid, oid_Root_class_oid, SIX_LOCK, LK_UNCOND_LOCK);
  if (lock_ret != LK_GRANTED)
    {
      ASSERT_ERROR_AND_SET (ret);
      goto end;
    }

  /* Get class info for the primary key */
  classrepr = heap_classrepr_get (thread_p, sort_args->fk_refcls_oid, NULL, NULL_REPRID, &classrepr_cacheindex);
  if (classrepr == NULL)
    {
      ret = ER_FK_INVALID;
      goto end;
    }

  /* Primary key index search prepare */
  ret = btree_prepare_bts (thread_p, &pk_bt_scan, sort_args->fk_refcls_pk_btid, &pk_isid, NULL, NULL, NULL, NULL,
			   NULL, false, NULL);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  /* Set the order. */
  is_fk_scan_desc = (sort_args->key_type->is_desc != pk_bt_scan.btid_int.key_type->is_desc);

  /* Get the corresponding leaf of the foreign key. */
  if (!is_fk_scan_desc)
    {
      /* Get first leaf vpid. */
      vpid = load_args->vpid_first_leaf;
    }
  else
    {
      /* Get the last leaf. Current leaf is the last one. */
      vpid = load_args->leaf.vpid;
    }

  /* Init slot id */
  pk_bt_scan.slot_id = 0;

  /* Check if there are any partitions on the primary key. */
  if (classrepr->has_partition_info > 0)
    {
      (void) partition_init_pruning_context (&pcontext);
      clear_pcontext = true;

      ret = partition_load_pruning_context (thread_p, sort_args->fk_refcls_oid, DB_PARTITIONED_CLASS, &pcontext);
      if (ret != NO_ERROR)
	{
	  goto end;
	}

      /* Get number of partitions. */
      part_count = pcontext.count - 1;	/* exclude partitioned table */

      assert (part_count <= MAX_PARTITIONS);

      partitions = (BTREE_SCAN_PART *) malloc (part_count * sizeof (BTREE_SCAN_PART));
      if (partitions == NULL)
	{
	  ASSERT_ERROR_AND_SET (ret);
	  goto end;
	}

      /* Init context of each partition using the root context. */
      for (i = 0; i < part_count; i++)
	{
	  memcpy (&partitions[i].pcontext, &pcontext, sizeof (PRUNING_CONTEXT));
	  memcpy (&partitions[i].bt_scan, &pk_bt_scan, sizeof (BTREE_SCAN));

	  partitions[i].bt_scan.btid_int.sys_btid = &partitions[i].btid;
	  BTID_SET_NULL (&partitions[i].btid);
	  partitions[i].header = NULL;
	  partitions[i].key_cnt = -1;
	}
    }

  while (true)
    {
      ret = btree_advance_to_next_slot_and_fix_page (thread_p, sort_args->btid, &vpid, &curr_fk_pageptr, &fk_slot_id,
						     &fk_key, &clear_fk_key, is_fk_scan_desc, &fk_node_key_cnt,
						     &fk_node_header, &mvcc_snapshot_dirty);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  break;
	}

      has_nulls = false;

      if (curr_fk_pageptr == NULL)
	{
	  /* Search has ended. */
	  break;
	}

      if (DB_IS_NULL (&fk_key))
	{
	  /* Only way to get this is by having no visible objects in the foreign key. */
	  /* Must be checked!! */
	  break;
	}

      /* Check for multi col nulls. */
      if (sort_args->n_attrs > 1)
	{
	  has_nulls = btree_multicol_key_has_null (&fk_key);
	}
      else
	{
	  /* TODO: unreachable case */
	  has_nulls = DB_IS_NULL (&fk_key);
	}

      if (has_nulls)
	{
	  /* ANSI SQL says
	   *
	   *  The choices for <match type> are MATCH SIMPLE, MATCH PARTIAL, and MATCH FULL; MATCH SIMPLE is the default.
	   *  There is no semantic difference between these choices if there is only one referencing column (and, hence,
	   *  only one referenced column). There is also no semantic difference if all referencing columns are not
	   *  nullable. If there is more than one referencing column, at least one of which is nullable, and
	   *  if no <referencing period specification> is specified, then the various <match type>s have the following
	   *  semantics:
	   *
	   *  MATCH SIMPLE: if at least one referencing column is null, then the row of the referencing table passes
	   *   the constraint check. If all referencing columns are not null, then the row passes the constraint check
	   *   if and only if there is a row of the referenced table that matches all the referencing columns.
	   *  MATCH PARTIAL: if all referencing columns are null, then the row of the referencing table passes
	   *   the constraint check. If at least one referencing columns is not null, then the row passes the constraint
	   *   check if and only if there is a row of the referenced table that matches all the non-null referencing
	   *   columns.
	   *  MATCH FULL: if all referencing columns are null, then the row of the referencing table passes
	   *   the constraint check. If all referencing columns are not null, then the row passes the constraint check
	   *   if and only if there is a row of the referenced table that matches all the referencing columns. If some
	   *   referencing column is null and another referencing column is non-null, then the row of the referencing
	   *   table violates the constraint check.
	   *
	   * In short, we don't provide options for <match type> and our behavior is <MATCH SIMPLE> which is
	   * the default behavior of ANSI SQL and the other (commercial) products.
	   */

	  /* Skip current key. */
	  continue;
	}

      if (has_deduplicate_key_col)
	{
	  assert (!DB_IS_NULL (&fk_key));
	  assert (DB_VALUE_DOMAIN_TYPE (&fk_key) == DB_TYPE_MIDXKEY);

	  DB_VALUE *new_ptr = (fk_key_ptr == &(new_fk_key[0])) ? &(new_fk_key[1]) : &(new_fk_key[0]);

	  pr_clear_value (new_ptr);
	  ret = btree_remake_reference_key_with_FK (thread_p, pk_bt_scan.btid_int.key_type, &fk_key, new_ptr);
	  if (ret != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      break;
	    }

	  if (btree_compare_key (fk_key_ptr, new_ptr, pk_bt_scan.btid_int.key_type, 1, 1, NULL) == DB_EQ)
	    {			/* Remove the added deduplicate_key_attr and it can be the same key. */
	      continue;
	    }

	  fk_key_ptr = new_ptr;
	}

      /* We got the value from the foreign key, now search through the primary key index. */
      found = false;

      if (partitions)
	{
	  COPY_OID (&pk_clsoid, sort_args->fk_refcls_oid);
	  BTID_COPY (&pk_btid, sort_args->fk_refcls_pk_btid);

	  /* Get the correct oid, btid and partition of the key we are looking for. */
	  ret = partition_prune_partition_index (&pcontext, fk_key_ptr, &pk_clsoid, &pk_btid, &pos);
	  if (ret != NO_ERROR)
	    {
	      break;
	    }

	  if (BTID_IS_NULL (&partitions[pos].btid))
	    {
	      /* No need to lock individual partitions here, since the partitioned table is already locked */
	      ret = partition_prune_unique_btid (&pcontext, fk_key_ptr, &pk_clsoid, &pk_dummy_hfid, &pk_btid);
	      if (ret != NO_ERROR)
		{
		  break;
		}

	      /* Update the partition BTID. */
	      BTID_COPY (&partitions[pos].btid, &pk_btid);
	    }

	  /* Save the old page, if any. */
	  if (pk_bt_scan.C_page != NULL)
	    {
	      old_page = pk_bt_scan.C_page;
	    }

	  /* Update references. */
	  pk_bt_scan = partitions[pos].bt_scan;
	  pk_node_key_cnt = partitions[pos].key_cnt;
	  pk_node_header = partitions[pos].header;
	}

      /* Search through the primary key index. */
      if (pk_bt_scan.C_page == NULL)
	{
	  /* No search has been initiated yet, we start from root. */
	  ret = btree_locate_key (thread_p, &pk_bt_scan.btid_int, fk_key_ptr, &pk_bt_scan.C_vpid, &pk_bt_scan.slot_id,
				  &pk_bt_scan.C_page, &found);
	  if (ret != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      break;
	    }
	  else if (!found)
	    {
	      /* Value was not found at all, it means the foreign key is invalid. */
	      val_print = pr_valstring (fk_key_ptr);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FK_INVALID, 2, sort_args->fk_name,
		      (val_print ? val_print : "unknown value"));
	      ret = ER_FK_INVALID;
	      db_private_free (thread_p, val_print);
	      break;
	    }
	  else
	    {
	      /* First unfix the old page if applicable. The new page was fixed in btree_locate_key. */
	      pgbuf_unfix_and_init_after_check (thread_p, old_page);

	      /* Make sure there is at least one visible object. */
	      ret = btree_is_slot_visible (thread_p, &pk_bt_scan.btid_int, pk_bt_scan.C_page, &mvcc_snapshot_dirty,
					   pk_bt_scan.slot_id, &pk_has_slot_visible);
	      if (ret != NO_ERROR)
		{
		  break;
		}

	      if (!pk_has_slot_visible)
		{
		  /* No visible object in current page, but the key was located here. Should not happen often. */
		  val_print = pr_valstring (fk_key_ptr);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FK_INVALID, 2, sort_args->fk_name,
			  (val_print ? val_print : "unknown value"));
		  ret = ER_FK_INVALID;
		  db_private_free (thread_p, val_print);
		  break;
		}

	      assert (pk_bt_scan.C_page != NULL);
	    }

	  /* Unfix old page, if any. */
	  pgbuf_unfix_and_init_after_check (thread_p, old_page);
	}
      else
	{
	  /* We try to resume the search in the current leaf. */
	  while (true)
	    {
	      ret = btree_advance_to_next_slot_and_fix_page (thread_p, &pk_bt_scan.btid_int, &pk_bt_scan.C_vpid,
							     &pk_bt_scan.C_page, &pk_bt_scan.slot_id, &pk_key,
							     &clear_pk_key, false, &pk_node_key_cnt, &pk_node_header,
							     &mvcc_snapshot_dirty);
	      if (ret != NO_ERROR)
		{
		  goto end;
		}

	      if (pk_bt_scan.C_page == NULL)
		{
		  /* The primary key has ended, but the value from foreign key was not found. */
		  /* Foreign key is invalid. Set error. */
		  val_print = pr_valstring (fk_key_ptr);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FK_INVALID, 2, sort_args->fk_name,
			  (val_print ? val_print : "unknown value"));
		  ret = ER_FK_INVALID;
		  db_private_free (thread_p, val_print);
		  goto end;
		}

	      /* We need to compare the current value with the new value from the primary key. */
	      compare_ret = btree_compare_key (&pk_key, fk_key_ptr, pk_bt_scan.btid_int.key_type, 1, 1, NULL);
	      if (compare_ret == DB_EQ)
		{
		  /* Found value, stop searching in pk. */
		  break;
		}
	      else if (compare_ret == DB_LT)
		{
		  /* No match yet. Advance in pk. */
		  continue;
		}
	      else
		{
		  /* Fk is invalid. Set error. */
		  val_print = pr_valstring (fk_key_ptr);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FK_INVALID, 2, sort_args->fk_name,
			  (val_print ? val_print : "unknown value"));
		  ret = ER_FK_INVALID;
		  db_private_free (thread_p, val_print);
		  goto end;
		}
	    }

	  if (pk_bt_scan.slot_id > pk_node_key_cnt)
	    {
	      old_page = pk_bt_scan.C_page;
	      pk_bt_scan.C_page = NULL;
	    }
	}

      if (partitions)
	{
	  /* Update references. */
	  partitions[pos].key_cnt = pk_node_key_cnt;
	  partitions[pos].header = pk_node_header;
	}

      if (found == true)
	{
	  btree_clear_key_value (&clear_fk_key, &fk_key);
	}

      btree_clear_key_value (&clear_pk_key, &pk_key);
    }

end:

  if (partitions)
    {
      for (i = 0; i < part_count; i++)
	{
	  pgbuf_unfix_and_init_after_check (thread_p, partitions[i].bt_scan.C_page);
	}

      free (partitions);
    }

  pgbuf_unfix_and_init_after_check (thread_p, old_page);
  pgbuf_unfix_and_init_after_check (thread_p, curr_fk_pageptr);
  pgbuf_unfix_and_init_after_check (thread_p, pk_bt_scan.C_page);

  btree_clear_key_value (&clear_fk_key, &fk_key);
  btree_clear_key_value (&clear_pk_key, &pk_key);

  pr_clear_value (&(new_fk_key[0]));
  pr_clear_value (&(new_fk_key[1]));

  if (clear_pcontext == true)
    {
      partition_clear_pruning_context (&pcontext);
    }

  if (classrepr != NULL)
    {
      heap_classrepr_free_and_init (classrepr, &classrepr_cacheindex);
    }

  return ret;
}

/*
 * btree_get_value_from_leaf_slot () -
 *
 *   return: NO_ERROR or error code.
 *   btid_int(in): The structure of the B-tree where the leaf resides.
 *   leaf_ptr(in): The leaf where the value needs to be extracted from.
 *   slot_id(in): The slot from where the value must be pulled.
 *   key(out): The value requested.
 *   clear_key(out): needs to clear key if set
 *
 */
static int
btree_get_value_from_leaf_slot (THREAD_ENTRY * thread_p, BTID_INT * btid_int, PAGE_PTR leaf_ptr, int slot_id,
				DB_VALUE * key, bool * clear_key)
{
  LEAF_REC leaf;
  int first_key_offset = 0;
  RECDES record;
  int ret = NO_ERROR;

  if (spage_get_record (thread_p, leaf_ptr, slot_id, &record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      ret = ER_FAILED;
      return ret;
    }

  ret = btree_read_record (thread_p, btid_int, leaf_ptr, &record, key, &leaf, BTREE_LEAF_NODE, clear_key,
			   &first_key_offset, PEEK_KEY_VALUE, NULL);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      return ret;
    }

  return ret;
}

/*
 * btree_advance_to_next_slot_and_fix_page () -
 *
 *   return: NO_ERROR or error code
 *   btid(in): B-tree structure
 *   vpid(in/out): VPID of the current page
 *   pg_ptr(in/out): Page pointer for the current page.
 *   slot_id(in/out): Slot id of the current/next value.
 *   key(out): Requested key.
 *   clear_key(out): needs to clear key if set.
 *   key_cnt(in/out): Number of keys in current page.
 *   header(in/out): The header of the current page.
 *   mvcc(in): Needed for visibility check.
 *
 *  Note:
 *      This function will advance to a next page without using conditional latches.
 *      Therefore, if this is used for a descending scan, it might not work properly
 *      unless concurrency is guaranteed.
 */
static int
btree_advance_to_next_slot_and_fix_page (THREAD_ENTRY * thread_p, BTID_INT * btid, VPID * vpid, PAGE_PTR * pg_ptr,
					 INT16 * slot_id, DB_VALUE * key, bool * clear_key, bool is_desc, int *key_cnt,
					 BTREE_NODE_HEADER ** header, MVCC_SNAPSHOT * mvcc)
{
  int ret = NO_ERROR;
  VPID next_vpid;
  PAGE_PTR page = *pg_ptr;
  BTREE_NODE_HEADER *local_header = *header;
  bool is_slot_visible = false;
  PAGE_PTR old_page = NULL;

  /* Clear current key, if any. */
  if (!DB_IS_NULL (key))
    {
      pr_clear_value (key);
    }

  if (page == NULL)
    {
      page = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (page == NULL)
	{
	  ASSERT_ERROR_AND_SET (ret);
	  return ret;
	}

      *slot_id = -1;
    }

  assert (page != NULL);

  /* Check page. */
  (void) pgbuf_check_page_ptype (thread_p, page, PAGE_BTREE);

  if (local_header == NULL)
    {
      /* Get the header of the page. */
      local_header = btree_get_node_header (thread_p, page);
    }

  if (*key_cnt == -1)
    {
      /* Get number of keys in page. */
      *key_cnt = btree_node_number_of_keys (thread_p, page);
    }

  /* If it is the first search. */
  if (*slot_id == -1)
    {
      *slot_id = is_desc ? (*key_cnt + 1) : 0;
    }

  /* Advance to next key. */
  int incr_decr_val = (is_desc ? -1 : 1);
  while (true)
    {
      *slot_id += incr_decr_val;
      assert (0 <= *slot_id);

      if (*slot_id == 0 || *slot_id >= *key_cnt + 1)
	{
	  next_vpid = is_desc ? local_header->prev_vpid : local_header->next_vpid;
	  if (VPID_ISNULL (&next_vpid))
	    {
	      /* No next page. unfix current one. */
	      pgbuf_unfix_and_init (thread_p, page);
	      break;
	    }
	  else
	    {
	      old_page = page;
	      page = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	      if (page == NULL)
		{
		  ASSERT_ERROR_AND_SET (ret);
		  return ret;
		}

	      /* unfix old page */
	      pgbuf_unfix_and_init (thread_p, old_page);

	      *slot_id = is_desc ? *key_cnt : 1;

	      /* Get the new header. */
	      local_header = btree_get_node_header (thread_p, page);

	      /* Get number of keys in new page. */
	      *key_cnt = btree_node_number_of_keys (thread_p, page);
	    }
	}

      if (mvcc != NULL)
	{
	  ret = btree_is_slot_visible (thread_p, btid, page, mvcc, *slot_id, &is_slot_visible);
	  if (ret != NO_ERROR)
	    {
	      return ret;
	    }

	  if (!is_slot_visible)
	    {
	      continue;
	    }
	}

      /* The slot is visible. Fall through and get the key. */
      break;
    }

  if (page != NULL)
    {
      ret = btree_get_value_from_leaf_slot (thread_p, btid, page, *slot_id, key, clear_key);
    }

  *header = local_header;
  *pg_ptr = page;

  return ret;
}

/*
 *  btree_is_slot_visible(): States if current slot is visible or not.
 *
 *  thread_p(in): Thread entry.
 *  btid(in): B-tree info.
 *  pg_ptr(in):	Page pointer.
 *  mvcc_snapshot(in): The MVCC snapshot.
 *  slot_id(in) : Slot id to be looked for.
 *  is_visible(out): True or False
 *
 *  return: error code if any error occurs.
 */
static int
btree_is_slot_visible (THREAD_ENTRY * thread_p, BTID_INT * btid, PAGE_PTR pg_ptr, MVCC_SNAPSHOT * mvcc_snapshot,
		       int slot_id, bool * is_visible)
{
  RECDES record;
  LEAF_REC leaf;
  int num_visible = 0;
  int key_offset = 0;
  int ret = NO_ERROR;
  bool dummy_clear_key;

  *is_visible = false;

  if (mvcc_snapshot == NULL)
    {
      /* Early out. */
      *is_visible = true;
      return ret;
    }

  /* Get the record. */
  if (spage_get_record (thread_p, pg_ptr, slot_id, &record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      ret = ER_FAILED;
      return ret;
    }

  /* Read the record. - no need of actual key value */
  ret = btree_read_record (thread_p, btid, pg_ptr, &record, NULL, &leaf, BTREE_LEAF_NODE, &dummy_clear_key,
			   &key_offset, PEEK_KEY_VALUE, NULL);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      return ret;
    }

  /* Get the number of visible items. */
  ret = btree_get_num_visible_from_leaf_and_ovf (thread_p, btid, &record, key_offset, &leaf, NULL, mvcc_snapshot,
						 &num_visible);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  if (num_visible > 0)
    {
      *is_visible = true;
    }

  return ret;
}

BTID *
xbtree_load_online_index (THREAD_ENTRY * thread_p, BTID * btid, const char *bt_name, TP_DOMAIN * key_type,
			  OID * class_oids, int n_classes, int n_attrs, int *attr_ids, int *attrs_prefix_length,
			  HFID * hfids, int unique_pk, int not_null_flag, OID * fk_refcls_oid, BTID * fk_refcls_pk_btid,
			  const char *fk_name, char *pred_stream, int pred_stream_size, char *func_pred_stream,
			  int func_pred_stream_size, int func_col_id, int func_attr_index_start, int ib_thread_count)
{
  int cur_class;
  BTID_INT btid_int;
  PRED_EXPR_WITH_CONTEXT *filter_pred = NULL;
  FUNCTION_INDEX_INFO func_index_info;
  DB_TYPE single_node_type = DB_TYPE_NULL;
  XASL_UNPACK_INFO *func_unpack_info = NULL;
  bool is_sysop_started = false;
  MVCC_SNAPSHOT *builder_snapshot = NULL;
  HEAP_SCANCACHE scan_cache;
  HEAP_CACHE_ATTRINFO attr_info;
  int ret = NO_ERROR;
  LOCK old_lock = SCH_M_LOCK;
  LOCK new_lock = IX_LOCK;
  LOG_TDES *tdes;
  int lock_ret;
  BTID *list_btid = NULL;
  int old_wait_msec;
  bool old_check_intr;
  SORT_ARGS tmp_args;

  memset (&tmp_args, 0x00, sizeof (SORT_ARGS));
  tmp_args.n_attrs = n_attrs;
  tmp_args.class_ids = class_oids;
  tmp_args.attr_ids = attr_ids;
  tmp_args.hfids = hfids;
  tmp_args.func_index_info = &func_index_info;

  func_index_info.expr = NULL;

  /* Check for robustness */
  if (!btid || !hfids || !class_oids || !attr_ids || !key_type)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_LOAD_FAILED, 0);
      return NULL;
    }

  btid_int.sys_btid = btid;
  btid_int.unique_pk = unique_pk;

#if !defined(NDEBUG)
  if (unique_pk)
    {
      assert (BTREE_IS_UNIQUE (btid_int.unique_pk));
      assert (BTREE_IS_PRIMARY_KEY (btid_int.unique_pk) || !BTREE_IS_PRIMARY_KEY (btid_int.unique_pk));
    }
#endif

  btid_int.key_type = key_type;
  VFID_SET_NULL (&btid_int.ovfid);
  btid_int.rev_level = BTREE_CURRENT_REV_LEVEL;
  btid_int.deduplicate_key_idx = dk_get_deduplicate_key_position (n_attrs, attr_ids, func_attr_index_start);
  COPY_OID (&btid_int.topclass_oid, &class_oids[0]);
  /*
   * for btree_range_search, part_key_desc is re-set at btree_initialize_bts
   */
  btid_int.part_key_desc = 0;

  /* init index key copy_buf info */
  btid_int.copy_buf = NULL;
  btid_int.copy_buf_len = 0;
  btid_int.nonleaf_key_type = btree_generate_prefix_domain (&btid_int);

  /* After building index acquire lock on table, the transaction has deadlock priority */
  tdes = LOG_FIND_CURRENT_TDES (thread_p);
  if (tdes)
    {
      tdes->has_deadlock_priority = true;
    }

  /* Acquire snapshot!! */
  builder_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (builder_snapshot == NULL)
    {
      goto error;
    }

  /* Alloc memory for btid list for unique indexes. */
  if (BTREE_IS_UNIQUE (unique_pk))
    {
      list_btid = (BTID *) malloc (n_classes * sizeof (BTID));
      if (list_btid == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, n_classes * sizeof (BTID));
	  ret = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}
    }

  /* Demote the locks for classes on which we want to load indices. */
  for (cur_class = 0; cur_class < n_classes; cur_class++)
    {
      ret = lock_demote_class_lock (thread_p, &class_oids[cur_class], new_lock, &old_lock);
      if (ret != NO_ERROR)
	{
	  goto error;
	}
    }

  for (cur_class = 0; cur_class < n_classes; cur_class++)
    {
      /* Reinitialize filter and function for each class, if is the case. This is needed in order to bring the regu
       * variables in same state like initial state. Clearing XASL does not bring the regu variables in initial state.
       * A issue is clearing the cache (see cache_dbvalp and cache_attrinfo).
       * We may have the option to try to correct clearing XASL (to have initial state) or destroy it and reinitalize it.
       * For now, we choose reinitialization, since is much simply, and is used also in non-online case.
       */
      if (pred_stream && pred_stream_size > 0)
	{
	  if (stx_map_stream_to_filter_pred (thread_p, &filter_pred, pred_stream, pred_stream_size) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      tmp_args.filter = filter_pred;

      if (func_pred_stream && func_pred_stream_size > 0)
	{
	  func_index_info.expr_stream = func_pred_stream;
	  func_index_info.expr_stream_size = func_pred_stream_size;
	  func_index_info.col_id = func_col_id;
	  func_index_info.attr_index_start = func_attr_index_start;
	  func_index_info.expr = NULL;
	  if (stx_map_stream_to_func_pred (thread_p, &func_index_info.expr, func_pred_stream,
					   func_pred_stream_size, &func_unpack_info))
	    {
	      goto error;
	    }
	}

      /* Start scancache */
      tmp_args.cur_class = cur_class;
      ret = bt_load_heap_scancache_start_for_attrinfo (thread_p, &tmp_args, &scan_cache, &attr_info, true);
      if (ret != NO_ERROR)
	{
	  goto error;
	}

      /* Assign the snapshot to the scan_cache. */
      scan_cache.mvcc_snapshot = builder_snapshot;

      ret = heap_get_btid_from_index_name (thread_p, &class_oids[cur_class], bt_name, btid_int.sys_btid);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  break;
	}

      /* For unique indices add to the list of btids for unique constraint checks. */
      if (BTREE_IS_UNIQUE (unique_pk))
	{
	  list_btid[cur_class].root_pageid = btid_int.sys_btid->root_pageid;
	  list_btid[cur_class].vfid.fileid = btid_int.sys_btid->vfid.fileid;
	  list_btid[cur_class].vfid.volid = btid_int.sys_btid->vfid.volid;
	}

      if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
	{
	  _er_log_debug (ARG_FILE_LINE, "DEBUG_BTREE: load start on class(%d, %d, %d), btid(%d, (%d, %d)).",
			 OID_AS_ARGS (&class_oids[cur_class]), BTID_AS_ARGS (btid_int.sys_btid));
	}

      /* Start the online index builder. */
      ret = online_index_builder (thread_p, &btid_int, &hfids[cur_class], &class_oids[cur_class], n_classes, attr_ids,
				  n_attrs, func_index_info, filter_pred, attrs_prefix_length, &attr_info, &scan_cache,
				  unique_pk, ib_thread_count, key_type);
      if (ret != NO_ERROR)
	{
	  break;
	}

      bt_load_heap_scancache_end_for_attrinfo (thread_p, &tmp_args, &scan_cache, &attr_info);
      bt_load_clear_pred_and_unpack (thread_p, &tmp_args, func_unpack_info);
      tmp_args.filter = filter_pred = NULL;
    }

  // We should recover the lock regardless of return code from online_index_builder.
  // Otherwise, we might be doomed to failure to abort the transaction.
  // We are going to do best to avoid lock promotion errors such as timeout and deadlocked.

  // never give up
  old_wait_msec = xlogtb_reset_wait_msecs (thread_p, LK_INFINITE_WAIT);
  old_check_intr = logtb_set_check_interrupt (thread_p, false);

  for (cur_class = 0; cur_class < n_classes; cur_class++)
    {
      /* Promote the lock to SCH_M_LOCK */
      /* we need to do this in a loop to retry in case of interruption */
      while (true)
	{
	  lock_ret = lock_object (thread_p, &class_oids[cur_class], oid_Root_class_oid, SCH_M_LOCK, LK_UNCOND_LOCK);
	  if (lock_ret == LK_GRANTED)
	    {
	      break;
	    }
#if defined (SERVER_MODE)
	  else if (lock_ret == LK_NOTGRANTED_DUE_ERROR)
	    {
	      if (er_errid () == ER_INTERRUPTED)
		{
		  // interruptions cannot be allowed here; lock must be promoted to either commit or rollback changes
		  er_clear ();
		  // make sure the transaction interrupt flag is cleared
		  logtb_set_tran_index_interrupt (thread_p, thread_p->tran_index, false);
		  // and retry
		  continue;
		}
	    }
	  else if (lock_ret == LK_NOTGRANTED_DUE_TIMEOUT && css_is_shutdowning_server ())
	    {
	      // server shutdown forced timeout; but consistency requires that we get the lock upgrade no matter what
	      er_clear ();
	      continue;
	    }
#endif // SERVER_MODE

	  // it is neither expected nor acceptable.
	  assert (0);
	}
    }

  // reset back
  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msec);
  (void) logtb_set_check_interrupt (thread_p, old_check_intr);

  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error;
    }

  if (BTREE_IS_UNIQUE (unique_pk))
    {
      for (cur_class = 0; cur_class < n_classes; cur_class++)
	{
	  /* Check if we have a unique constraint violation for unique indexes. */
	  ret =
	    btree_online_index_check_unique_constraint (thread_p, &list_btid[cur_class], bt_name,
							&class_oids[cur_class]);
	  if (ret != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      btid = NULL;
	      goto error;
	    }
	}
    }

  assert (tmp_args.scancache_inited == false && tmp_args.attrinfo_inited == false);

  if (list_btid != NULL)
    {
      free (list_btid);
      list_btid = NULL;
    }

  logpb_force_flush_pages (thread_p);

  /* TODO: Is this all right? */
  /* Invalidate snapshot. */
  if (builder_snapshot != NULL)
    {
      logtb_invalidate_snapshot_data (thread_p);
    }

  return btid;

error:
  bt_load_heap_scancache_end_for_attrinfo (thread_p, &tmp_args, &scan_cache, &attr_info);
  bt_load_clear_pred_and_unpack (thread_p, &tmp_args, func_unpack_info);

  if (list_btid != NULL)
    {
      free (list_btid);
    }

  /* Invalidate snapshot. */
  if (builder_snapshot != NULL)
    {
      logtb_invalidate_snapshot_data (thread_p);
    }

  return NULL;
}

// *INDENT-OFF*
// Refer to src/parser/csql_grammar.y:19872
REGISTER_WORKERPOOL (online_index, []() { return 16; });

static int
online_index_builder (THREAD_ENTRY * thread_p, BTID_INT * btid_int, HFID * hfids, OID * class_oids, int n_classes,
		      int *attrids, int n_attrs, FUNCTION_INDEX_INFO func_idx_info,
		      PRED_EXPR_WITH_CONTEXT * filter_pred, int *attrs_prefix_length, HEAP_CACHE_ATTRINFO * attr_info,
		      HEAP_SCANCACHE * scancache, int unique_pk, int ib_thread_count, TP_DOMAIN * key_type)
{
  int ret = NO_ERROR, eval_res;
  OID cur_oid;
  RECDES cur_record;
  int cur_class;
  SCAN_CODE sc;
  FUNCTION_INDEX_INFO *p_func_idx_info;
  PR_EVAL_FNC filter_eval_fnc;
  DB_TYPE single_node_type = DB_TYPE_NULL;
  int attr_offset;
  DB_VALUE *p_dbvalue;
  int *p_prefix_length;
  uint64_t tasks_started = 0;
  char midxkey_buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_midxkey_buf;
  index_builder_loader_context load_context;
  bool is_parallel = ib_thread_count > 0;
  std::atomic<int> num_keys = {0}, num_oids = {0}, num_nulls = {0};

  std::unique_ptr<index_builder_loader_task> load_task = NULL;

  assert (ib_thread_count <= 16);

  // a worker pool is built only of loading is done in parallel
  cubthread::worker_pool_type *ib_workpool =
    is_parallel ? thread_create_worker_pool (ib_thread_count, 1, "online-index", load_context) : NULL;
  // m_log = btree_is_worker_pool_logging_true ()

  aligned_midxkey_buf = PTR_ALIGN (midxkey_buf, MAX_ALIGNMENT);
  p_func_idx_info = func_idx_info.expr ? &func_idx_info : NULL;
  filter_eval_fnc = (filter_pred != NULL) ? eval_fnc (thread_p, filter_pred->pred, &single_node_type) : NULL;

  /* Get the first entry from heap. */
  cur_class = 0;
  OID_SET_NULL (&cur_oid);
  cur_oid.volid = hfids[cur_class].vfid.volid;

  /* Do not let the page fixed after an extract. */
  scancache->cache_last_fix_page = false;

  load_context.m_has_error = false;
  load_context.m_error_code = NO_ERROR;
  load_context.m_tasks_executed = 0UL;
  load_context.m_key_type = key_type;
  load_context.m_conn = thread_p->conn_entry;

  PERF_UTIME_TRACKER time_online_index = PERF_UTIME_TRACKER_INITIALIZER;

  PERF_UTIME_TRACKER_START (thread_p, &time_online_index);

  p_prefix_length = (attrs_prefix_length) ?  &(attrs_prefix_length[0]) : NULL;	

  /* Start extracting from heap. */
  for (;;)
    {
      DB_VALUE dbvalue;

      db_make_null (&dbvalue);

      /* Scan from heap and insert into the index. */
      attr_offset = cur_class * n_attrs;

      cur_record.data = NULL;

      sc = heap_next (thread_p, &hfids[cur_class], &class_oids[cur_class], &cur_oid, &cur_record, scancache, COPY);
      if (sc != S_SUCCESS)
        {
          if (sc != S_END)
            {
                if (sc == S_ERROR)
                  {
                     ASSERT_ERROR_AND_SET (ret);
                     break;
                  }
               assert (false);
            }
            break;
        }

      /* Make sure the scan was a success. */      
      assert (!OID_ISNULL (&cur_oid));

      if (filter_pred)
	{
	  ret = heap_attrinfo_read_dbvalues (thread_p, &cur_oid, &cur_record, filter_pred->cache_pred);
	  if (ret != NO_ERROR)
	    {
	      break;
	    }

	  eval_res = (*filter_eval_fnc) (thread_p, filter_pred->pred, NULL, &cur_oid);
	  if (eval_res == V_ERROR)
	    {
	      ret = ER_FAILED;
	      break;
	    }
	  else if (eval_res != V_TRUE)
	    {
	      continue;
	    }
	}

/* Generate the key : provide key_type domain - needed for compares during sort */
      p_dbvalue = heap_attrinfo_generate_key (thread_p, n_attrs, &attrids[attr_offset], p_prefix_length, attr_info,
					      &cur_record, &dbvalue, aligned_midxkey_buf, p_func_idx_info, key_type, &cur_oid);
      if (p_dbvalue == NULL)
	{
	  ret = ER_FAILED;
	  break;
	}

      /* Dispatch the insert operation */
      if (load_task == NULL)
        {
          // create a new task
	  load_task.reset (new index_builder_loader_task (btid_int->sys_btid, &class_oids[cur_class], unique_pk,
							  load_context, num_keys, num_oids, num_nulls));
        }
      if (load_task->add_key (p_dbvalue, cur_oid) == index_builder_loader_task::BATCH_FULL)
        {
          // send task to worker pool for execution
	  thread_get_manager ()->push_task (ib_workpool, load_task.release ());
	  /* Increment tasks started. */
	  tasks_started++;
        }

      /* Clear index key. */
      pr_clear_value (p_dbvalue);

      /* Check for possible errors. */
      if (load_context.m_has_error)
	{
	  /* Also stop all threads. */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IB_ERROR_ABORT, 0);
	  ret = load_context.m_error_code;
	  break;
	}
    }

  /* Check if the worker pool is empty */
  if (ret == NO_ERROR)
    {
      if (load_task != NULL && load_task->has_keys ())
        {
          // one last task
          thread_get_manager ()->push_task (ib_workpool, load_task.release ());
          /* Increment tasks started. */
          tasks_started++;
        }
      do
	{
	  bool dummy_continue_checking = true;

	  if (load_context.m_has_error != NO_ERROR)
	    {
	      /* Also stop all threads. */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IB_ERROR_ABORT, 0);
	      ret = load_context.m_error_code;
	      break;
	    }

	  /* Wait for threads to finish. */
	  thread_sleep (10);

	  /* Check for interrupts. */
	  if (logtb_is_interrupted (thread_p, true, &dummy_continue_checking))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	      ret = ER_INTERRUPTED;
	      break;
	    }
	}
      while (load_context.m_tasks_executed != tasks_started);
    }

  PERF_UTIME_TRACKER_TIME (thread_p, &time_online_index, PSTAT_BT_ONLINE_LOAD);

  thread_get_manager ()->destroy_worker_pool (ib_workpool);

  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      logtb_tran_update_btid_unique_stats (thread_p, btid_int->sys_btid, num_keys, num_oids, num_nulls);
    }

  return ret;
}

static bool
btree_is_worker_pool_logging_true ()
{
  return cubthread::is_logging_configured (cubthread::LOG_WORKER_POOL_INDEX_BUILDER);
}

void
index_builder_loader_context::on_create (context_type &context)
{
  context.claim_system_worker ();
  context.conn_entry = m_conn;
}

void
index_builder_loader_context::on_retire (context_type &context)
{
  context.retire_system_worker ();
  context.conn_entry = NULL;
}

void
index_builder_loader_context::on_recycle (context_type &context)
{
  context.tran_index = LOG_SYSTEM_TRAN_INDEX;
}

index_builder_loader_task::index_builder_loader_task (const BTID *btid, const OID *class_oid, int unique_pk,
						      index_builder_loader_context &load_context,
						      std::atomic<int> &num_keys, std::atomic<int> &num_oids,
						      std::atomic<int> &num_nulls)
  : m_load_context (load_context)
  , m_insert_list (load_context.m_key_type)
  , m_num_keys (num_keys)
  , m_num_oids (num_oids)
  , m_num_nulls (num_nulls)
{
  BTID_COPY (&m_btid, btid);
  COPY_OID (&m_class_oid, class_oid);
  m_unique_pk = unique_pk;
  m_load_context.m_has_error = false;
  m_memsize = 0;
}

index_builder_loader_task::~index_builder_loader_task ()
{
  cubmem::switch_to_global_allocator_and_call ([this] { clear_keys (); });
}

index_builder_loader_task::batch_key_status
index_builder_loader_task::add_key (const DB_VALUE *key, const OID &oid)
{
  if (DB_IS_NULL (key) || btree_multicol_key_is_null (const_cast<DB_VALUE *>(key)))
    {
      /* We do not store NULL keys, but we track them for unique indexes;
       * for non-unique, just skip row */
      if (BTREE_IS_UNIQUE (m_unique_pk))
        {
          ++m_insert_list.m_ignored_nulls_cnt;
        }
      return BATCH_CONTINUE;
    }

  size_t entry_size = m_insert_list.add_key (key, oid);

  m_memsize += entry_size;

  return (m_memsize > (size_t) prm_get_bigint_value (PRM_ID_IB_TASK_MEMSIZE)) ? BATCH_FULL : BATCH_CONTINUE;
}

bool
index_builder_loader_task::has_keys () const
{
  return !m_insert_list.m_keys_oids.empty ();
}

void
index_builder_loader_task::clear_keys ()
{
  for (auto &key_oid : m_insert_list.m_keys_oids)
    {
      pr_clear_value (&key_oid.m_key);
    }
}

void
index_builder_loader_task::execute (cubthread::entry &thread_ref)
{
  int ret = NO_ERROR;
  size_t key_count = 0;
  LOG_TRAN_BTID_UNIQUE_STATS *p_unique_stats;

  /* Check for possible errors set by the other threads. */
  if (m_load_context.m_has_error)
    {
      return;
    }

  PERF_UTIME_TRACKER time_insert_task = PERF_UTIME_TRACKER_INITIALIZER;
  PERF_UTIME_TRACKER_START (&thread_ref, &time_insert_task);

  PERF_UTIME_TRACKER time_prepare_task = PERF_UTIME_TRACKER_INITIALIZER;
  PERF_UTIME_TRACKER_START (&thread_ref, &time_prepare_task);

  m_insert_list.prepare_list ();

  PERF_UTIME_TRACKER_TIME (&thread_ref, &time_prepare_task, PSTAT_BT_ONLINE_PREPARE_TASK);

  while (m_insert_list.m_curr_pos < (int) m_insert_list.m_sorted_keys_oids.size ())
    {
      ret = btree_online_index_list_dispatcher (&thread_ref, &m_btid, &m_class_oid, &m_insert_list,
					        m_unique_pk, BTREE_OP_ONLINE_INDEX_IB_INSERT, NULL);

      if (ret != NO_ERROR)
	{
	  if (!m_load_context.m_has_error.exchange (true))
	    {
	      m_load_context.m_error_code = ret;
	      // TODO: We need a mechanism to also copy the error message!!
	    }
	  break;
	}

      key_count++;
    }

  p_unique_stats = logtb_tran_find_btid_stats (&thread_ref, &m_btid, false);
  if (p_unique_stats != NULL)
    {
      /* Cumulates and resets statistics */
      m_num_keys += p_unique_stats->tran_stats.num_keys;
      m_num_oids += p_unique_stats->tran_stats.num_oids + m_insert_list.m_ignored_nulls_cnt;
      m_num_nulls += p_unique_stats->tran_stats.num_nulls + m_insert_list.m_ignored_nulls_cnt;

      p_unique_stats->tran_stats.num_keys = 0;
      p_unique_stats->tran_stats.num_oids = 0;
      p_unique_stats->tran_stats.num_nulls = 0;
    }

  PERF_UTIME_TRACKER_TIME (&thread_ref, &time_insert_task, PSTAT_BT_ONLINE_INSERT_TASK);

  /* Increment tasks executed. */
  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE, "Finished task; loaded %zu keys\n", key_count);
    }

  m_load_context.m_tasks_executed++;
}
// *INDENT-ON*
