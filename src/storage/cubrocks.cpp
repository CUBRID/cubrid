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

#include "porting.h"
#include "dbtype_def.h"
#include "heap_file.h"
#include "cubrocks.hpp"
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
  opt.cf.memtable_prefix_bloom_size_ratio = 0.05;


  /* use CRC32 if the machine supports acceleration functions */
  opt.table.checksum = rocksdb::kCRC32c;
  opt.table.data_block_index_type = rocksdb::BlockBasedTableOptions::kDataBlockBinaryAndHash;
  opt.table.filter_policy.reset (rocksdb::NewBloomFilterPolicy (10, false));
  opt.table.index_type = rocksdb::BlockBasedTableOptions::kBinarySearchWithFirstKey;
  opt.table.block_size = 16 * 1024;
  opt.table.cache_index_and_filter_blocks = true;
  opt.table.cache_index_and_filter_blocks_with_high_priority = true;
  opt.table.block_cache = rocksdb::NewLRUCache (512 * 1024 * 1024LL, 6);
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
 
  oid = *((OID *) &transactions[tran_index].reserved_next);
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
      txn_name << tran_index << ":" << trid;
      transactions[tran_index].txn = db->BeginTransaction (write_options);
      transactions[tran_index].txn->SetName (txn_name.str ());
      assert (transactions[tran_index].txn != nullptr);
    }
}

void
cubrocks::context::kv_tran_prepare (int tran_index)
{
  rocksdb::Status status;

  assert (alive);
  assert (tran_index < MAX_NTRANS);

  status = transactions[tran_index].txn->Prepare ();
  if (!status.ok ())
  {
    std::cout << status.ToString () << std::endl;
  }
  assert (status.ok ());
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

  //kv_tran_prepare (tran_index);

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
  rocksdb::Slice value (context->recdes_p->data, context->recdes_p->length);

  oid = (context->type == HEAP_OPERATION_INSERT) ? kv_get_void (tran_index) : context->oid;
  oid_uint64 = *((UINT64 *) &oid);

  /* memory ordering */
  * ((short *) virtual_key) = context->class_oid.volid;
  * ((int *) (virtual_key + 2)) = context->class_oid.pageid;
  * ((short *) (virtual_key + 6)) = context->class_oid.slotid;

  * ((UINT64 *) (virtual_key + 8)) = htobe64 (oid_uint64);

  if (context->type == HEAP_OPERATION_INSERT || context->type == HEAP_OPERATION_UPDATE)
    {
      status = transactions[tran_index].txn->Put (key, value).ok ();
    }
  else
    {
      /* range removal or table drop should be handled at a higher level.                  */

      /* TODO: need to implement table drop and range deletion.                            */
      /* table drop doesn't actually happen in STORAGE, but it can work logically since    */
      /* heap doesn't reuse slots. and it means that inserted records will still be there. */

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

  readopt.prefix_same_as_start = true;
  readopt.io_activity = rocksdb::Env::IOActivity::kDBIterator;
//  readopt.rate_limiter_priority = rocksdb::Env::IOPriority::IO_HIGH;

//  readopt.pin_data = true;
  readopt.fill_cache = false;
  readopt.async_io = true;
  readopt.adaptive_readahead = true; 

//  readopt.readahead_size = 2 * 1024 * 1024;

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
}

void
cubrocks::context::kv_close ()
{
  /* it is not clear whether "Flush -> WaitForCompact" should be called in that order.
   * also, check if DestroyColumnFamilyHandle should be called. */
  assert (alive);

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
