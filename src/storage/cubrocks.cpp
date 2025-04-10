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
 * cubrocks.cpp - rocksdb for cubrid
 */

#include "object_primitive.h"
#ident "$Id$"

#include <string>
#include <memory>
#include <iostream>
#include <thread>
#include <assert.h>
#include <sstream>

#include <arpa/inet.h>

#include "rocksdb/version.h"
#include "rocksdb/iterator.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/compression_type.h"
#include "rocksdb/memtablerep.h"
#include "rocksdb/db.h"

#include "locator_sr.h"
#include "porting.h"
#include "query_evaluator.h"
#include "query_reevaluation.hpp"
#include "fetch.h"
#include "btree_load.h"
#include "dbtype.h"
#include "dbtype_def.h"
#include "heap_file.h"
#include "cubrocks.hpp"
#include "thread_manager.hpp"
#include "log_impl.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubrocks
{
  context _ctx;
  context *ctx = &_ctx;
}

void
cubrocks::kv_version (void)
{
  std::cout << std::endl << "RocksDB version: " << ROCKSDB_MAJOR << "." << ROCKSDB_MINOR << "." << ROCKSDB_PATCH <<
	    std::endl;
}

std::string
cubrocks::kv_postfix (char *path)
{
  std::string str (path);

  str += "_rocksdb";
  return str;
}

cubrocks::context::context ()
{
  bool status;

  kv_config ();

  status = transactions_initialize ();
  assert (status);

  alive = false;

  /* set virtual counter */
  SET_OID ((OID *) &virtual_counter, /* volid */ 32767, /* page id */ 0, /* slot id */ 0);
}

cubrocks::context::~context ()
{
  if (alive)
    {
      kv_close ();
    }
}

void
cubrocks::context::kv_config (void)
{
  opt.txndb.default_lock_timeout = -1;
  opt.txndb.transaction_lock_timeout = -1;

  opt.db.IncreaseParallelism (std::thread::hardware_concurrency ());
  opt.db.max_open_files = -1;
  opt.db.enable_pipelined_write = true;
  opt.db.avoid_unnecessary_blocking_io = true;

  opt.db.max_total_wal_size = 4 * 1024 * 1024 * 1024LL;

  opt.db.bytes_per_sync = 16777216;
  opt.db.wal_bytes_per_sync = 4194304;

  opt.db.use_direct_reads = true;
  opt.db.use_direct_io_for_flush_and_compaction = true;

  opt.db.max_subcompactions = 4;
  opt.db.compaction_readahead_size = 16 * 1024 * 1024;

  /* this should be set for pair experiment */
  opt.cf.compression = rocksdb::kNoCompression;

  opt.cf.write_buffer_size = 256 * 1024 * 1024;
  opt.cf.max_write_buffer_number = 2;

  opt.cf.max_bytes_for_level_base = 512 * 1024 * 1024;

  opt.cf.level0_stop_writes_trigger = 30;

  opt.cf.target_file_size_base = 32 * 1024 * 1024;

  opt.cf.optimize_filters_for_hits = true;
  opt.cf.memtable_whole_key_filtering = true;
  opt.cf.memtable_prefix_bloom_size_ratio = 0.05;


  /* use CRC32 if the machine supports acceleration functions */
  opt.table.checksum = rocksdb::kCRC32c;
  opt.table.data_block_index_type = rocksdb::BlockBasedTableOptions::kDataBlockBinaryAndHash;
  opt.table.filter_policy.reset (rocksdb::NewBloomFilterPolicy (10, false));
  opt.table.index_type = rocksdb::BlockBasedTableOptions::kBinarySearchWithFirstKey;
  opt.table.block_size = 16 * 1024;
  opt.table.cache_index_and_filter_blocks = true;
  opt.table.cache_index_and_filter_blocks_with_high_priority = true;
  opt.table.block_cache = rocksdb::NewLRUCache (5 * 1024 * 1024 * 1024LL, 6);
  opt.table.use_delta_encoding = false;

  opt.table.metadata_cache_options.top_level_index_pinning = rocksdb::PinningTier::kFlushedAndSimilar;
  opt.table.metadata_cache_options.partition_pinning = rocksdb::PinningTier::kFlushedAndSimilar;
  opt.table.metadata_cache_options.unpartitioned_pinning = rocksdb::PinningTier::kFlushedAndSimilar;

  opt.cf.table_factory.reset (NewBlockBasedTableFactory (opt.table));
  opt.cf.prefix_extractor.reset (rocksdb::NewFixedPrefixTransform (8));

  opt.cf_descriptor.push_back (rocksdb::ColumnFamilyDescriptor ("default", opt.cf));
}

bool
cubrocks::context::is_alive (void)
{
  return alive;
}

bool
cubrocks::context::is_tran_started (int tran_index)
{
  return transactions[tran_index].txn != nullptr;
}

void
cubrocks::context::kv_reserve_void (int tran_index, int count)
{
  assert (alive);
  assert (tran_index < MAX_NTRANS);

  UINT64 vcounter, vcounter_next;

  do
    {
      vcounter = virtual_counter;
      vcounter_next = vcounter + count;
    }
  while (!ATOMIC_CAS_64 (&virtual_counter, vcounter, vcounter_next));

  transactions[tran_index].reserved_count = count;
  transactions[tran_index].reserved_next = vcounter;
}

OID
cubrocks::context::kv_get_void (int tran_index)
{
  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (transactions[tran_index].reserved_count > 0);

  OID oid;

  oid = * ((OID *) &transactions[tran_index].reserved_next);
  transactions[tran_index].reserved_count--;
  transactions[tran_index].reserved_next++;

  return oid;
}

void
cubrocks::context::kv_tran_start (int tran_index, int trid)
{
  rocksdb::WriteOptions write_options;
  std::ostringstream txn_name;

  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (tran_index == LOG_SYSTEM_TRAN_INDEX || transactions[tran_index].txn == nullptr);

  if (transactions[tran_index].txn == nullptr)
    {
      write_options.memtable_insert_hint_per_batch = true;
      write_options.sync = true;

      transactions[tran_index].txn = db->BeginTransaction (write_options);
      assert (transactions[tran_index].txn != nullptr);
    }
}

void
cubrocks::context::kv_tran_commit (int tran_index)
{
  bool status;

  assert (alive);
  assert (tran_index < MAX_NTRANS);

  if (transactions[tran_index].txn == nullptr)
    {
      assert (tran_index == LOG_SYSTEM_TRAN_INDEX);
      return ;
    }

  status = transactions[tran_index].txn->Commit ().ok ();
  delete transactions[tran_index].txn;
  transactions[tran_index].txn = nullptr;

  assert (status);
}

void
cubrocks::context::kv_tran_abort (int tran_index)
{
  bool status;

  assert (alive);
  assert (tran_index < MAX_NTRANS);

  if (transactions[tran_index].txn == nullptr)
    {
      assert (tran_index == LOG_SYSTEM_TRAN_INDEX);
      return ;
    }

  status = transactions[tran_index].txn->Rollback ().ok ();
  delete transactions[tran_index].txn;
  transactions[tran_index].txn = nullptr;

  assert (status);
}

int
cubrocks::context::kv_logical_write (int tran_index, HEAP_OPERATION_CONTEXT *context)
{
  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (transactions[tran_index].txn != nullptr);

  assert (context != NULL);
  assert (context->type == HEAP_OPERATION_INSERT || context->type == HEAP_OPERATION_UPDATE
	  || context->type == HEAP_OPERATION_DELETE);
  assert ((context->recdes_p != NULL && context->recdes_p->data != NULL) || context->type == HEAP_OPERATION_DELETE);
  assert (!OID_ISNULL (&context->class_oid));

  /* This operation should be insert for instance. */

  bool status;
  OID oid;
  UINT64 oid_uint64;
  char virtual_key[16];
  rocksdb::Slice key (virtual_key, 16);

  oid = (context->type == HEAP_OPERATION_INSERT) ? kv_get_void (tran_index) : context->oid;
  oid_uint64 = * ((UINT64 *) &oid);

  /* memory ordering */
  * ((short *) virtual_key) = context->class_oid.volid;
  * ((int *) (virtual_key + 2)) = context->class_oid.pageid;
  * ((short *) (virtual_key + 6)) = context->class_oid.slotid;

  if (context->type == HEAP_OPERATION_INSERT || context->type == HEAP_OPERATION_UPDATE)
    {
      rocksdb::Slice value (context->recdes_p->data, context->recdes_p->length);

      if (context->type == HEAP_OPERATION_INSERT)
	{
	  * ((UINT64 *) (virtual_key + 8)) = htobe64 (oid_uint64);
	}
      else
	{
	  * ((UINT64 *) (virtual_key + 8)) = oid_uint64;
	}

      status = transactions[tran_index].txn->Put (key, value).ok ();
    }
  else
    {
      /* range removal or table drop should be handled at a higher level.                  */

      /* TODO: need to implement table drop and range deletion.                            */
      /* table drop doesn't actually happen in STORAGE, but it can work logically since    */
      /* heap doesn't reuse slots. and it means that inserted records will still be there. */
      * ((UINT64 *) (virtual_key + 8)) = oid_uint64;

      status = transactions[tran_index].txn->Delete (key).ok ();
    }
  assert (status);

  COPY_OID (&context->res_oid, &oid);

  return NO_ERROR;
}

void
cubrocks::context::kv_scan_start (HEAP_SCANCACHE *scan_cache)
{
  assert (alive);
  assert (scan_cache != NULL);

  /* scan_cache has no constructor, so we can't figure out the member in scan_cache is garbage value or not. */
  /* BUT, since this function is called from heap_scancache_start family, we can consider the values in scan_cache is just garbage. */
  rocksdb::ReadOptions readopt;

  /* read committed */
  readopt.tailing = true;

  readopt.prefix_same_as_start = true;
  readopt.io_activity = rocksdb::Env::IOActivity::kDBIterator;

//  readopt.fill_cache = false;
  readopt.async_io = true;
  readopt.readahead_size = 2 * 1024 * 1024;

  readopt.background_purge_on_iterator_cleanup = true;

  scan_cache->kv_iter = db->NewIterator (readopt);
  assert (scan_cache->kv_iter != nullptr);
}

void
cubrocks::context::kv_scan_start_with_bound (HEAP_SCANCACHE *scan_cache, OID *class_oid, DB_VALUE *lower,
    DB_VALUE *upper)
{
  assert (alive);
  assert (scan_cache != NULL);

  /* scan_cache has no constructor, so we can't figure out the member in scan_cache is garbage value or not. */
  /* BUT, since this function is called from heap_scancache_start family, we can consider the values in scan_cache is just garbage. */
  rocksdb::ReadOptions readopt;
  char *key_buf;
  int key_size;
  int dummy;

  if (lower != NULL)
    {
      key_size = kv_get_key_size (lower);
      key_buf = (char *) malloc (key_size * sizeof (char));
      kv_make_key_with_PK (key_buf, key_size, class_oid, lower, dummy);

      scan_cache->kv_lower = rocksdb::Slice (key_buf, key_size);
      readopt.iterate_lower_bound = &scan_cache->kv_lower;
    }
  if (upper != NULL)
    {
      key_size = kv_get_key_size (upper);
      key_buf = (char *) malloc (key_size * sizeof (char));
      kv_make_key_with_PK (key_buf, key_size, class_oid, upper, dummy);

      scan_cache->kv_upper = rocksdb::Slice (key_buf, key_size);
      readopt.iterate_upper_bound = &scan_cache->kv_upper;
    }

  /* read committed */
  readopt.tailing = true;

  readopt.prefix_same_as_start = true;
  readopt.io_activity = rocksdb::Env::IOActivity::kDBIterator;

//  readopt.fill_cache = false;
  readopt.async_io = true;
  readopt.readahead_size = 2 * 1024 * 1024;

  readopt.background_purge_on_iterator_cleanup = true;

  scan_cache->kv_iter = db->NewIterator (readopt);
  assert (scan_cache->kv_iter != nullptr);
}

void
cubrocks::context::kv_scan_end (HEAP_SCANCACHE *scan_cache)
{
  assert (alive);

  assert (scan_cache != NULL);
  assert (scan_cache->kv_iter != nullptr);

  delete scan_cache->kv_iter;
  scan_cache->kv_iter = nullptr;

  /*
  if (!scan_cache->kv_lower.empty ())
    {
      free ((void *) scan_cache->kv_lower.data ());
    }
  if (!scan_cache->kv_upper.empty ())
    {
      free ((void *) scan_cache->kv_upper.data ());
    }
    */
}

SCAN_CODE
cubrocks::context::kv_logical_scan (int tran_index, OID *class_oid, OID *next_oid, RECDES *recdes,
				    HEAP_SCANCACHE *scan_cache, int ispeeking)
{
  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (transactions[tran_index].txn != nullptr);

  assert (scan_cache != NULL);
  assert (scan_cache->kv_iter != nullptr);

  /* I think the caller should not use PEEK for ispeeking at this function.
   * However, even if it is called with PEEK, it may not be a matter
   * as the data update must be performed by heap_update_logical. */
  assert (ispeeking == PEEK || ispeeking == COPY);

  char prefix_buf[8];
  rocksdb::Slice prefix (prefix_buf, 8);
  SCAN_CODE scan = S_SUCCESS;

  if (OID_ISNULL (next_oid))
    {
      if (!OID_ISNULL (&scan_cache->node.class_oid))
	{
	  class_oid = &scan_cache->node.class_oid;
	}

      /* memory ordering */
      * ((short *) prefix_buf) = class_oid->volid;
      * ((int *) (prefix_buf + 2)) = class_oid->pageid;
      * ((short *) (prefix_buf + 6)) = class_oid->slotid;

      scan_cache->kv_iter->Seek (prefix);
    }
  else
    {
      scan_cache->kv_iter->Next ();
    }

  if (scan_cache->kv_iter->Valid ())
    {
      /* simpler expression is possible using C-style type casts. */
      *next_oid = *reinterpret_cast<OID *> (const_cast<char *> (&scan_cache->kv_iter->key ().data ()[8]));

      recdes->type = REC_HOME;
      recdes->length = scan_cache->kv_iter->value ().size ();

      /* NOTE: using PinnableSlice can reduce the latency by skipping the process of allocating the heap and copying data into the heap. */
      if (ispeeking == COPY)
	{
	  scan_cache->assign_recdes_to_area (*recdes, (size_t) DB_PAGESIZE);

	  memcpy (recdes->data, scan_cache->kv_iter->value ().data (), recdes->length);
	}
      else
	{
	  recdes->data = const_cast<char *> (scan_cache->kv_iter->value ().data ());
	  recdes->area_size = recdes->length;
	}
    }
  else
    {
      if (!scan_cache->kv_iter->status ().ok ())
	{
	  assert (false);
	}

      OID_SET_NULL (next_oid);
      recdes->data = NULL;
      recdes->length = 0;
      recdes->area_size = 0;

      scan = S_END;
    }

  return scan;
}

SCAN_CODE
cubrocks::context::kv_lock_and_get (int tran_index, OID *class_oid, OID *oid, RECDES *recdes,
				    HEAP_SCANCACHE *scan_cache, int ispeeking)
{
  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (transactions[tran_index].txn != nullptr);

  assert (scan_cache != NULL);

  assert (ispeeking == PEEK || ispeeking == COPY);

  char virtual_key[16];
  rocksdb::Slice key (virtual_key, 16);

  if (OID_ISNULL (class_oid))
    {
      assert_release (false);
    }

  /* memory ordering */
  * ((short *) virtual_key) = class_oid->volid;
  * ((int *) (virtual_key + 2)) = class_oid->pageid;
  * ((short *) (virtual_key + 6)) = class_oid->slotid;

  * ((UINT64 *) (virtual_key + 8)) = * ((UINT64 *) oid);

  return kv_lock_and_get (tran_index, key, recdes, scan_cache, ispeeking);
}

SCAN_CODE
cubrocks::context::kv_lock_and_get (int tran_index, rocksdb::Slice &key, RECDES *recdes,
				    HEAP_SCANCACHE *scan_cache, int ispeeking)
{
  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (transactions[tran_index].txn != nullptr);

  assert (scan_cache != NULL);

  assert (ispeeking == PEEK || ispeeking == COPY);

  rocksdb::ReadOptions read_options;
  rocksdb::Status status;

  status = transactions[tran_index].txn->GetForUpdate (read_options, key, &transactions[tran_index].pin);
  if (!status.ok ())
    {
      if (status.IsNotFound ())
	{
	  /* the object is removed when this thread waits for locking */
	  return S_DOESNT_EXIST;
	}

      /* currently timedout is not set so... */
      /* abort when status is not ok */
      std::cout << status.ToString () << std::endl;
      assert_release (false);
    }

  recdes->type = REC_HOME;
  recdes->length = transactions[tran_index].pin.length ();
  if (ispeeking == COPY)
    {
      scan_cache->assign_recdes_to_area (*recdes, (size_t) DB_PAGESIZE);

      memcpy (recdes->data, transactions[tran_index].pin.data (), recdes->length);
    }
  else
    {
      recdes->data = const_cast<char *> (transactions[tran_index].pin.data ());
      recdes->area_size = recdes->length;
    }

  return S_SUCCESS;
}

void
cubrocks::context::kv_lock_release (int tran_index, OID *class_oid, OID *oid)
{
  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (transactions[tran_index].txn != nullptr);

  char virtual_key[16];
  rocksdb::Slice key (virtual_key, 16);

  /* memory ordering */
  * ((short *) virtual_key) = class_oid->volid;
  * ((int *) (virtual_key + 2)) = class_oid->pageid;
  * ((short *) (virtual_key + 6)) = class_oid->slotid;

  * ((UINT64 *) (virtual_key + 8)) = * ((UINT64 *) oid);

  kv_lock_release (tran_index, key);
}

void
cubrocks::context::kv_lock_release (int tran_index, rocksdb::Slice &key)
{
  transactions[tran_index].txn->UndoGetForUpdate (key);
}

SCAN_CODE
cubrocks::context::kv_get (int tran_index, OID *class_oid, OID *oid, RECDES *recdes, HEAP_SCANCACHE *scan_cache,
			   int ispeeking)
{
  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (transactions[tran_index].txn != nullptr);

  assert (ispeeking == PEEK || ispeeking == COPY);

  rocksdb::ReadOptions read_options;
  char virtual_key[16];
  rocksdb::Status status;
  rocksdb::Slice key (virtual_key, 16);

  if (OID_ISNULL (class_oid))
    {
      assert_release (false);
    }

  /* memory ordering */
  * ((short *) virtual_key) = class_oid->volid;
  * ((int *) (virtual_key + 2)) = class_oid->pageid;
  * ((short *) (virtual_key + 6)) = class_oid->slotid;

  * ((UINT64 *) (virtual_key + 8)) = * ((UINT64 *) oid);

  return kv_get (tran_index, key, recdes, scan_cache, ispeeking);
}

SCAN_CODE
cubrocks::context::kv_get (int tran_index, rocksdb::Slice &key, RECDES *recdes, HEAP_SCANCACHE *scan_cache,
			   int ispeeking)
{
  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (transactions[tran_index].txn != nullptr);

  assert (ispeeking == PEEK || ispeeking == COPY);

  rocksdb::ReadOptions read_options;
  rocksdb::Status status;

  status = transactions[tran_index].txn->Get (read_options, key, &transactions[tran_index].pin);
  if (!status.ok ())
    {
      if (status.IsNotFound ())
	{
	  /* the object is removed when this thread waits for locking */
	  return S_DOESNT_EXIST;
	}

      /* currently timedout is not set so... */
      /* abort when status is not ok */
      std::cout << status.ToString () << std::endl;
      assert_release (false);
    }

  recdes->type = REC_HOME;
  recdes->length = transactions[tran_index].pin.length ();
  if (ispeeking == COPY)
    {
      scan_cache->assign_recdes_to_area (*recdes, (size_t) DB_PAGESIZE);

      memcpy (recdes->data, transactions[tran_index].pin.data (), recdes->length);
    }
  else
    {
      recdes->data = const_cast<char *> (transactions[tran_index].pin.data ());
      recdes->area_size = recdes->length;
    }

  return S_SUCCESS;
}

int
cubrocks::context::kv_logical_write_with_PK (int tran_index, HEAP_OPERATION_CONTEXT *context)
{
  /* NOTE: the key will be consisted with PK like below.        */
  /* there is no virtual oid, but this key guarantees uniqeness */
  /*							        */
  /* ---------------------------------			        */
  /* |  cls_oid  |  memcomparble-PK  |			        */
  /* ---------------------------------			        */

  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (transactions[tran_index].txn != nullptr);

  assert (context != NULL);
  assert (context->type == HEAP_OPERATION_INSERT || context->type == HEAP_OPERATION_UPDATE
	  || context->type == HEAP_OPERATION_DELETE);
  assert (context->type == HEAP_OPERATION_INSERT || context->pk != NULL);
  assert (!OID_ISNULL (&context->class_oid));

  const int btid_index = 0;

  THREAD_ENTRY *thread_p;
  char dbvalue_buf[DBVAL_BUFSIZE + MAX_ALIGNMENT], *aligned_buf;
  char key_buf[DBVAL_BUFSIZE];
  int num_found, num_btids;
  HEAP_CACHE_ATTRINFO index_attrinfo;
  HEAP_IDX_ELEMENTS_INFO idx_info;
  DB_VALUE *key_dbvalue;
  DB_VALUE dbvalue;
  rocksdb::Slice key, value;
  bool status;
  BTID btid;
  int key_size;
  int error_code;

  key_dbvalue = NULL;
  db_make_null (&dbvalue);

  aligned_buf = PTR_ALIGN (dbvalue_buf, MAX_ALIGNMENT);

  thread_p = thread_get_thread_entry_info ();
  num_found = heap_attrinfo_start_with_index (thread_p, &context->class_oid, NULL, &index_attrinfo, &idx_info, false);
  num_btids = idx_info.num_btids;

  if (num_found == 0)
    {
      return ER_FAILED;
    }
  else if (num_found < 0)
    {
      assert_release (false);
    }

  assert (idx_info.has_single_col);
  assert (num_btids == 1);
  assert (index_attrinfo.last_classrepr->indexes[btid_index].type == BTREE_PRIMARY_KEY);

  if (context->type == HEAP_OPERATION_DELETE)
    {
      kv_make_key_with_PK (key_buf, DBVAL_BUFSIZE, &context->class_oid, context->pk, key_size);
      key = rocksdb::Slice (key_buf, key_size);

      status = transactions[tran_index].txn->Delete (key).ok ();
      assert (status);
    }
  else
    {
      /* INSERT or UPDATE */
      assert (context->recdes_p != NULL && context->recdes_p->data != NULL);

      error_code = heap_attrinfo_read_dbvalues (thread_p, &context->oid, context->recdes_p, &index_attrinfo);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	}

      key_dbvalue =
	      heap_attrvalue_get_key (thread_p, btid_index, &index_attrinfo, context->recdes_p, &btid, &dbvalue, aligned_buf,
				      NULL, NULL, &context->oid, false);
      if (key_dbvalue == NULL)
	{
	  assert_release (false);
	}

      /* TODO: add dup key check */

      if (context->pk && context->type == HEAP_OPERATION_UPDATE)
	{
	  if (strcmp (db_get_string (context->pk), db_get_string (key_dbvalue)) != 0)
	    {
	      /* if PK must be changed, previous key should be deleted (context->pk) */
	      kv_make_key_with_PK (key_buf, DBVAL_BUFSIZE, &context->class_oid, context->pk, key_size);
	      key = rocksdb::Slice (key_buf, key_size);

	      status = transactions[tran_index].txn->Delete (key).ok ();
	      assert (status);
	    }
	}

      /* new from record */
      kv_make_key_with_PK (key_buf, DBVAL_BUFSIZE, &context->class_oid, key_dbvalue, key_size);
      key = rocksdb::Slice (key_buf, key_size);
      value = rocksdb::Slice (context->recdes_p->data, context->recdes_p->length);

      status = transactions[tran_index].txn->Put (key, value).ok ();
      assert (status);
    }

  heap_attrinfo_end (thread_p, &index_attrinfo);

  if (key_dbvalue == &dbvalue)
    {
      pr_clear_value (&dbvalue);
      key_dbvalue = NULL;
    }

  return NO_ERROR;
}

int
cubrocks::context::kv_resolve_index_key (SCAN_ID *scan_id)
{
  THREAD_ENTRY *thread_p;
  KEY_VAL_RANGE *key_vals;
  KEY_RANGE *key_ranges;
  INDX_SCAN_ID *iscan_id;
  indx_info *indx_infop;
  BTREE_SCAN *bts;
  int error_code;
  int key_cnt;
  int i;

  thread_p = thread_get_thread_entry_info ();

  iscan_id = &scan_id->s.isid;
  indx_infop = iscan_id->indx_info;
  bts = &iscan_id->bt_scan;
  key_cnt = indx_infop->key_info.key_cnt;

  /* key values */
  key_vals = scan_id->s.isid.key_vals;

  /* key ranges */
  key_ranges = scan_id->s.isid.indx_info->key_info.key_ranges;

  /* make DB_VALUE key values from KEY_VALS key ranges */
  for (i = 0; i < key_cnt; i++)
    {
      /* initialize DB_VALUE first for error case */
      key_vals[i].range = NA_NA;
      db_make_null (&key_vals[i].key1);
      db_make_null (&key_vals[i].key2);
      key_vals[i].is_truncated = false;
      key_vals[i].num_index_term = 0;

      key_vals[i].range = key_ranges[i].range;
      if (key_vals[i].range == INF_INF)
	{
	  continue;
	}

      error_code =
	      scan_regu_key_to_index_key (thread_p, &key_ranges[i], &key_vals[i], iscan_id, bts->btid_int.key_type,
					  scan_id->vd);

      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	}

      assert_release (key_vals[i].num_index_term > 0);
    }

  /* eliminating duplicated keys and merging ranges are required even though the query optimizer does them because
   * the search keys or ranges could be unbound values at optimization step such as join attribute */
  if (indx_infop->range_type == R_KEYLIST)
    {
      /* eliminate duplicated keys in the search key list */
      key_cnt = iscan_id->key_cnt = check_key_vals (key_vals, key_cnt, eliminate_duplicated_keys);
    }
  else if (indx_infop->range_type == R_RANGELIST)
    {
      /* merge search key ranges */
      key_cnt = iscan_id->key_cnt = check_key_vals (key_vals, key_cnt, merge_key_ranges);
    }

  /* if is order by skip and first column is descending, the order will be reversed so reverse the key ranges to be
   * desc. */
  if ((indx_infop->range_type == R_KEYLIST || indx_infop->range_type == R_RANGELIST)
      && ((indx_infop->orderby_desc && indx_infop->orderby_skip)
	  || (indx_infop->groupby_desc && indx_infop->groupby_skip)))
    {
      /* in both cases we should reverse the key lists if we have a reverse order by or group by which is skipped */
      check_key_vals (key_vals, key_cnt, reverse_key_list);
    }

  if (key_cnt < 0)
    {
      assert_release (false);
    }

  return key_cnt;
}

int
cubrocks::context::kv_assist_index_key (SCAN_ID *scan_id)
{
  INDX_SCAN_ID *isidp;
  indx_info *indx_infop;
  KEY_VAL_RANGE *key_vals;
  RANGE range;
  int i;

  isidp = &scan_id->s.isid;
  indx_infop = isidp->indx_info;
  key_vals = isidp->key_vals;

  for (i = 0; i < isidp->key_cnt; i++)
    {
      switch (indx_infop->range_type)
	{
	case R_KEY:
	  range = key_vals[0].range;
	  if (range == NA_NA)
	    {
	      isidp->curr_keyno = isidp->key_cnt;
	      continue;
	    }
	  if (isidp->key_cnt != 1)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	      return ER_FAILED;
	    }
	  break;

	case R_KEYLIST:
	  range = key_vals[i].range;
	  if (range == NA_NA)
	    {
	      isidp->curr_keyno = isidp->key_cnt;
	      continue;
	    }
	  break;

	case R_RANGE:
	  range = key_vals[0].range;

	  if (range == NA_NA || range == INF_INF)
	    {
	      isidp->curr_keyno = isidp->key_cnt;
	      continue;
	    }
	  if (isidp->key_cnt != 1 || range < GE_LE || range > INF_INF)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	      return ER_FAILED;
	    }
	  if (range >= GE_INF && range <= GT_INF)
	    {
	      pr_clear_value (&key_vals[0].key2);
	      PRIM_SET_NULL (&key_vals[0].key2);
	    }
	  if (range >= INF_LE && range <= INF_LT)
	    {
	      pr_clear_value (&key_vals[0].key1);
	      PRIM_SET_NULL (&key_vals[0].key1);
	    }
	  if (key_vals[0].is_truncated == true)
	    {
	      range = GE_LE;
	    }
	  key_vals[0].range = range;
	  break;

	case R_RANGELIST:
	  range = key_vals[i].range;

	  if (range == NA_NA || range == INF_INF)
	    {
	      isidp->curr_keyno = isidp->key_cnt;
	      continue;
	    }
	  if (range < GE_LE || range > INF_INF)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	      return ER_FAILED;
	    }
	  if (range >= GE_INF && range <= GT_INF)
	    {
	      pr_clear_value (&key_vals[i].key2);
	      PRIM_SET_NULL (&key_vals[i].key2);
	    }
	  if (range >= INF_LE && range <= INF_LT)
	    {
	      pr_clear_value (&key_vals[i].key1);
	      PRIM_SET_NULL (&key_vals[i].key1);
	    }
	  if (key_vals[i].is_truncated == true)
	    {
	      range = GE_LE;
	    }
	  key_vals[i].range = range;
	  break;

	default:
	  assert_release (false);
	}
    }

  return NO_ERROR;
}

SCAN_CODE
cubrocks::context::kv_logical_scan_with_PK (int tran_index, SCAN_ID *scan_id)
{
  THREAD_ENTRY *thread_p;
  INDX_SCAN_ID *isidp;
  indx_info *indx_infop;
  FILTER_INFO data_filter;
  KEY_VAL_RANGE *key_vals;
  int key_cnt;
  DB_LOGICAL ev_res;
  rocksdb::Slice key;
  RECDES recdes = RECDES_INITIALIZER;
  char key_buf[DBVAL_BUFSIZE];
  int key_size;
  SCAN_CODE scan;
  int i;
  MVCC_REC_HEADER dummy_mvcc =
  {
    .mvcc_ins_id = 0,
  };
  OID dummy_oid =
  {
    .pageid = -1,
    .slotid = -1,
    .volid = -1
  };

  assert (!OID_ISNULL (&scan_id->s.isid.cls_oid));

  thread_p = thread_get_thread_entry_info ();

  isidp = &scan_id->s.isid;
  indx_infop = isidp->indx_info;

  /* multi range optimization safe guard : fall-back to normal output (OID list or covering index instead of "on the
   * fly" lists), if sorting column is not yet set at this stage; also 'grouped' is not supported */
  if (isidp->multi_range_opt.use && (isidp->multi_range_opt.sort_att_idx == NULL || scan_id->grouped))
    {
      isidp->multi_range_opt.use = false;
      scan_id->scan_stats.multi_range_opt = false;
    }

  /* set data filter information */
  scan_init_filter_info (&data_filter, &isidp->scan_pred, &isidp->pred_attrs, scan_id->val_list, scan_id->vd,
			 &isidp->cls_oid, 0, NULL, NULL, NULL);

  key_vals = isidp->key_vals;
  key_cnt = isidp->key_cnt;
  if (isidp->curr_keyno == -1)
    {
      key_cnt = kv_resolve_index_key (scan_id);
      if (kv_assist_index_key (scan_id) != NO_ERROR ||
	  key_cnt < 1 || !isidp->key_vals)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	  scan = S_ERROR;
	  goto clear_and_exit;
	}

      isidp->curr_keyno = 0;
    }

  /* clear top n */
  if (isidp->multi_range_opt.use && isidp->multi_range_opt.cnt > 0)
    {
      /* reset any previous results for multiple range optimization */
      int i;

      for (i = 0; i < isidp->multi_range_opt.cnt; i++)
	{
	  if (isidp->multi_range_opt.top_n_items[i] != NULL)
	    {
	      pr_clear_value (& (isidp->multi_range_opt.top_n_items[i]->index_value));
	      db_private_free_and_init (thread_p, isidp->multi_range_opt.top_n_items[i]);
	    }
	}

      isidp->multi_range_opt.cnt = 0;
    }

scan_and_advacne:
  /* if the end of this scan */
  while (1)
    {
      if (isidp->curr_keyno >= key_cnt)
	{
	  scan = S_END;
	  goto clear_and_exit;
	}

      /* get next data */
      switch (indx_infop->range_type)
	{
	case R_KEY:
	  scan_id->scan_stats.key_qualified_rows++;

	  kv_make_key_with_PK (key_buf, DBVAL_BUFSIZE, &isidp->cls_oid, &key_vals[0].key1, key_size);
	  key = rocksdb::Slice (key_buf, key_size);

	  if (kv_get (tran_index, key, &recdes, &isidp->scan_cache, scan_id->fixed) == S_DOESNT_EXIST)
	    {
	      /* skip this key value */
	      isidp->curr_keyno++;
	      continue;
	    }

	  /* evaluate the predicates to see if the object qualifies */
	  ev_res = eval_data_filter (thread_p, isidp->curr_oidp, &recdes, &isidp->scan_cache, &data_filter);
	  ev_res = update_logical_result (thread_p, ev_res, (int *) &scan_id->qualification);
	  if (ev_res == V_ERROR)
	    {
	      scan = S_ERROR;
	      goto clear_and_exit;
	    }
	  else if (ev_res != V_TRUE)
	    {
	      /* skip this key value */
	      isidp->curr_keyno++;
	      continue;
	    }

	  /* it is not oid but this should be set to upper scope */
	  isidp->oids_count = 1;
	  /* give PK for temp */
	  pr_clone_value (&key_vals[0].key1, &isidp->pk_val);
	  /* advance */
	  isidp->curr_keyno++;

	  /* make tuple */
	  goto fall_through;

	case R_RANGE:
	case R_KEYLIST:
	case R_RANGELIST:
	  scan_id->scan_stats.key_qualified_rows++;

	  /* get data */
	  //scan_id->s.isid.scan_cache.kv_iter;

	  /* it is not oid but this should be set to upper scope */
	  isidp->oids_count = 0;
	  /* advance */
	  isidp->curr_keyno++;

	  return S_END;

	default:
	  scan = S_ERROR;
	  goto clear_and_exit;
	}
    }

fall_through:

  /* recdes */
  assert (recdes.data != NULL);
  assert (!key.empty ());

  if (scan_id->mvcc_select_lock_needed)
    {
      UPDDEL_MVCC_COND_REEVAL upd_reev;
      MVCC_SCAN_REEV_DATA mvcc_sel_reev_data;
      MVCC_REEV_DATA mvcc_reev_data;

      upd_reev.init (*scan_id);
      mvcc_sel_reev_data.set_filters (upd_reev);
      mvcc_sel_reev_data.qualification = &scan_id->qualification;
      mvcc_reev_data.set_scan_reevaluation (mvcc_sel_reev_data);

      scan =
	      kv_lock_and_get (tran_index, key, &recdes, &isidp->scan_cache, scan_id->fixed);
      if (scan == S_DOESNT_EXIST)
	{
	  /* no match, advance */
	  goto scan_and_advacne;
	}

      ev_res =
	      locator_mvcc_reev_cond_and_assignment (thread_p, &isidp->scan_cache, &mvcc_reev_data, &dummy_mvcc,
		  &dummy_oid, &recdes);
      if (ev_res != V_TRUE)
	{
	  cubrocks::ctx->kv_lock_release (tran_index, key);
	  if (ev_res == V_ERROR)
	    {
	      return S_ERROR;
	    }
	  /* no match, advance */
	  goto scan_and_advacne;
	}
      if (mvcc_reev_data.filter_result == V_FALSE)
	{
	  /* no match, advance */
	  goto scan_and_advacne;
	}
    }

  scan_id->scan_stats.data_qualified_rows++;
  if (isidp->rest_regu_list)
    {
      /* read the rest of the values from the heap into the attribute cache */
      if (heap_attrinfo_read_dbvalues (thread_p, isidp->curr_oidp, &recdes, isidp->rest_attrs.attr_cache) != NO_ERROR)
	{
	  scan = S_ERROR;
	  goto clear_and_exit;
	}

      /* fetch the rest of the values from the object instance */
      if (scan_id->val_list)
	{
	  if (fetch_val_list (thread_p, isidp->rest_regu_list, scan_id->vd, &isidp->cls_oid, isidp->curr_oidp, NULL,
			      PEEK) != NO_ERROR)
	    {
	      scan = S_ERROR;
	      goto clear_and_exit;
	    }
	}
    }

  return S_SUCCESS;

clear_and_exit:

  for (i = 0; isidp->curr_keyno <= key_cnt && i < key_cnt; i++)
    {
      pr_clear_value (&key_vals[i].key1);
      pr_clear_value (&key_vals[i].key2);
    }
  isidp->curr_keyno++;

  return scan;
}

void
cubrocks::context::kv_create (std::string path)
{
  assert (!alive);

  opt.db.create_if_missing = true;
  opt.db.error_if_exists = true;

  /* db will be closed in context::close ( ... ) that is called from boot_.._finalize */
  alive = rocksdb::TransactionDB::Open (opt.db, opt.txndb, path, opt.cf_descriptor, &opt.cf_handles, &db).ok();
  assert (alive);
}

void
cubrocks::context::kv_open (std::string path)
{
  assert (!alive);

  opt.db.create_if_missing = false;
  opt.db.error_if_exists = false;

  alive = rocksdb::TransactionDB::Open (opt.db, opt.txndb, path, opt.cf_descriptor, &opt.cf_handles, &db).ok();
  assert (alive);

  /* restore the virtual oid */
  kv_restore_void ();
}

void
cubrocks::context::kv_close ()
{
  /* it is not clear whether "Flush -> WaitForCompact" should be called in that order.
   * also, check if DestroyColumnFamilyHandle should be called. */
  assert (alive);

  /* remember the virtual oid */
  kv_store_void ();

  /* finalize */
  transactions_finalize ();

  rocksdb::WaitForCompactOptions opt_compact;
  rocksdb::FlushOptions opt_flush;

  opt_flush.wait = true;
  if (!db->Flush (opt_flush, opt.cf_handles).ok ())
    {
      assert (false);
    }

  /* release all references */
  for (auto cf_handle : opt.cf_handles)
    {
      if (cf_handle->GetName () != rocksdb::kDefaultColumnFamilyName)
	{
	  db->DropColumnFamily (cf_handle);
	}
    }

  /* release itself */
  for (auto cf_handle : opt.cf_handles)
    {
      db->DestroyColumnFamilyHandle (cf_handle);
    }

  opt_compact.close_db = true;
  if (!db->WaitForCompact (opt_compact).ok ())
    {
      assert (false);
    }

  delete db;
  db = nullptr;

  alive = false;
}

void
cubrocks::context::kv_destroy (std::string path)
{
  assert (!alive);

  rocksdb::Options options;

  if (!rocksdb::DestroyDB (path, options).ok())
    {
      assert (false);
    }

  db = nullptr;

  alive = false;
}

void
cubrocks::context::kv_store_void ()
{
  char void_key[16] = { 0, };
  rocksdb::Slice key (void_key, 16);
  rocksdb::Slice value ((char *) &virtual_counter, 8);
  rocksdb::Status status;

  status = db->Put (rocksdb::WriteOptions (), key, value);
  assert (status.ok ());
}

void
cubrocks::context::kv_restore_void ()
{
  char void_key[16] = { 0, };
  rocksdb::Slice key (void_key, 16);
  std::string value;
  rocksdb::Status status;

  status = db->Get (rocksdb::ReadOptions (), key, &value);
  if (status.ok ())
    {
      assert (value.length () == 8);

      virtual_counter = * ((UINT64 *) value.data ());
    }
  else
    {
      assert (status.IsNotFound ());
    }
}

void
cubrocks::context::kv_make_key_with_PK (char *buf, int buf_size, OID *class_oid, DB_VALUE *pk_value, int &key_size)
{
  DB_TYPE pk_type;

  assert (buf != NULL);
  assert (class_oid != NULL);
  assert (pk_value != NULL);

  /* prefix */
  * ((short *) buf) = class_oid->volid;
  * ((int *) (buf + 2)) = class_oid->pageid;
  * ((short *) (buf + 6)) = class_oid->slotid;

  key_size = sizeof (OID);
  pk_type = DB_VALUE_DOMAIN_TYPE (pk_value);
  /* write PK to key */
  /* have to care about each memory ordering. */
  switch (pk_type)
    {
    case DB_TYPE_STRING:
    case DB_TYPE_CHAR:
      memcpy (buf + key_size, db_get_string (pk_value), db_get_string_size (pk_value));
      key_size += db_get_string_size (pk_value);
      if (key_size > buf_size)
	{
	  assert_release (false);
	}
      break;

    default:
      /* if you want to handle pk other than above, need to implement that case */
      assert_release (false);
      break;
    }
}

int
cubrocks::context::kv_get_key_size (DB_VALUE *pk_value)
{
  DB_TYPE pk_type;
  int key_size;

  key_size = sizeof (OID);
  pk_type = DB_VALUE_DOMAIN_TYPE (pk_value);
  /* write PK to key */
  /* have to care about each memory ordering. */
  switch (pk_type)
    {
    case DB_TYPE_STRING:
    case DB_TYPE_CHAR:
      key_size += db_get_string_size (pk_value);
      break;

    default:
      /* if you want to handle pk other than above, need to implement that case */
      assert_release (false);
      break;
    }

  return key_size;
}

bool
cubrocks::context::transactions_initialize (void)
{
  int i;

  if ((transactions = new kv_transaction[MAX_NTRANS]) == nullptr)
    {
      return false;
    }

  for (i = 0; i < MAX_NTRANS; i++)
    {
      transactions[i].reserved_next = 0;
      transactions[i].reserved_count = 0;

      transactions[i].txn = nullptr;
    }

  return true;
}

void
cubrocks::context::transactions_finalize (void)
{
  bool status;
  int i;

  if (!alive)
    {
      return ;
    }

  for (i = 0; i < MAX_NTRANS; i++)
    {
      if (transactions[i].txn != nullptr)
	{
	  status = transactions[i].txn->Rollback ().ok ();
	  assert (status);
	  delete transactions[i].txn;
	  transactions[i].txn = nullptr;
	}
    }

  delete[] transactions;
}

