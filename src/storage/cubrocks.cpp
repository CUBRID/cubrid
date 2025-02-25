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
#include <assert.h>

#include "rocksdb/version.h"
#include "rocksdb/iterator.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/slice_transform.h"

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
  SET_OID(&virtual_counter, /* volid */ 10, /* page id */ 0, /* slot id */ 0);
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
  opt.db.bytes_per_sync = 1048576;
  opt.db.max_background_jobs = 6;

  opt.cf.level_compaction_dynamic_level_bytes = true;
  opt.cf.compaction_pri = ROCKSDB_NAMESPACE::kMinOverlappingRatio;
  opt.cf.compaction_style = ROCKSDB_NAMESPACE::kCompactionStyleLevel;
  opt.cf.write_buffer_size = 67108864;           // 64MB
  opt.cf.max_write_buffer_number = 3;
  opt.cf.target_file_size_base = 67108864;         // 64MB
  opt.cf.level0_file_num_compaction_trigger = 8;
  opt.cf.level0_slowdown_writes_trigger = 17;
  opt.cf.level0_stop_writes_trigger = 24;
  opt.cf.num_levels = 4;
  opt.cf.max_bytes_for_level_base = 536870912;      // 512MB
  opt.cf.max_bytes_for_level_multiplier = 8;

  opt.table.block_size = 16 * 1024;
  opt.table.cache_index_and_filter_blocks = true;
  opt.table.pin_l0_filter_and_index_blocks_in_cache = true;
  opt.table.block_cache = rocksdb::NewLRUCache (512 * 1024 * 1024, 8);
  opt.cf.table_factory.reset (NewBlockBasedTableFactory (opt.table));
  opt.cf.prefix_extractor.reset (rocksdb::NewCappedPrefixTransform (8));

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

OID
cubrocks::context::kv_get_virtual_count (void)
{
  OID oid = virtual_counter;

  virtual_counter.slotid++;
  if (virtual_counter.slotid >= 30)
  {
    virtual_counter.pageid++;
    virtual_counter.slotid = 0;
  }
  return oid;
}

void
cubrocks::context::kv_tran_start (int tran_index)
{
  rocksdb::WriteOptions write_options;

  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (tran_index == LOG_SYSTEM_TRAN_INDEX || transactions[tran_index].txn == nullptr);

  if (transactions[tran_index].txn == nullptr)
    {
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
cubrocks::context::kv_logical_insert (int tran_index, HEAP_OPERATION_CONTEXT * context)
{
  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (transactions[tran_index].txn != nullptr);

  assert (context != NULL);
  assert (context->type == HEAP_OPERATION_INSERT);
  assert (context->recdes_p != NULL && context->recdes_p->data != NULL);
  assert (!OID_ISNULL (&context->class_oid));

  /* This operation should be insert for instance. */

  bool status;
  OID oid;
  char virtual_key[16];
  rocksdb::Slice key (virtual_key, 16);
  rocksdb::Slice value (context->recdes_p->data, context->recdes_p->length);

  oid = cubrocks::ctx->kv_get_virtual_count ();

  /* memory ordering */
  *((short *) virtual_key) = context->class_oid.volid;
  *((int *) (virtual_key + 2)) = context->class_oid.pageid;
  *((short *) (virtual_key + 6)) = context->class_oid.slotid;

  *((short *) (virtual_key + 8)) = oid.volid;
  *((int *) (virtual_key + 10)) = oid.pageid;
  *((short *) (virtual_key + 14)) = oid.slotid;

  status = transactions[tran_index].txn->Put (key, value).ok ();
  assert (status);
  
  COPY_OID (&context->res_oid, &oid);

  return NO_ERROR;
}

void
cubrocks::context::kv_scan_start (HEAP_SCANCACHE * scan_cache)
{
  assert (alive);

  assert (scan_cache != NULL);

  /* scan_cache has no constructor, so we can't figure out the member in scan_cache is garbage value or not. */
  /* BUT, since this function is called from heap_scancache_start family, we can consider the values in scan_cache is just garbage. */

  scan_cache->kv_readopt.prefix_same_as_start = true;
  scan_cache->kv_readopt.io_activity = rocksdb::Env::IOActivity::kDBIterator;
  scan_cache->kv_readopt.snapshot = db->GetSnapshot ();
  assert (scan_cache->kv_readopt.snapshot != nullptr);

  scan_cache->kv_iter = db->NewIterator (scan_cache->kv_readopt);
  assert (scan_cache->kv_iter != nullptr);
}

void
cubrocks::context::kv_scan_end (HEAP_SCANCACHE * scan_cache)
{
  assert (alive);

  assert (scan_cache != NULL);
  assert (scan_cache->kv_iter != nullptr);
  assert (scan_cache->kv_readopt.snapshot != nullptr);
  
  delete scan_cache->kv_iter;
  db->ReleaseSnapshot (scan_cache->kv_readopt.snapshot);

  scan_cache->kv_iter = nullptr;
  scan_cache->kv_readopt.snapshot = nullptr;
}

SCAN_CODE 
cubrocks::context::kv_logical_scan (int tran_index, OID * class_oid, OID * next_oid, RECDES * recdes, HEAP_SCANCACHE * scan_cache, int ispeeking)
{
  assert (alive);
  assert (tran_index < MAX_NTRANS);
  assert (transactions[tran_index].txn != nullptr);

  assert (scan_cache != NULL);
  assert (scan_cache->kv_iter != nullptr);
  assert (scan_cache->kv_readopt.snapshot != nullptr);

  /* I think the caller should not use PEEK for ispeeking at this function.
   * However, even if it is called with PEEK, it may not be a matter
   * as the data update must be performed by heap_update_logical. */
  assert (ispeeking == PEEK || ispeeking == COPY);

  char prefix_buf[8];
  rocksdb::Slice prefix (prefix_buf, 8);
  SCAN_CODE scan = S_SUCCESS;

  if (!OID_ISNULL (&scan_cache->node.class_oid))
  {
    class_oid = &scan_cache->node.class_oid;
  }

  /* memory ordering */
  *((short *) prefix_buf) = class_oid->volid;
  *((int *) (prefix_buf + 2)) = class_oid->pageid;
  *((short *) (prefix_buf + 6)) = class_oid->slotid;

  if (OID_ISNULL (next_oid))
  {
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

    /* TODO: using PinnableSlice can reduce the latency by skipping the process of allocating the heap and copying data into the heap. */
    scan_cache->assign_recdes_to_area (*recdes, (size_t) DB_PAGESIZE);

    recdes->type = REC_HOME;
    recdes->length = scan_cache->kv_iter->value ().size ();

    memcpy (recdes->data, scan_cache->kv_iter->value ().data (), recdes->length);
  }
  else
  { 
    OID_SET_NULL (next_oid);
    recdes->data = NULL;
    recdes->length = 0;
    recdes->area_size = 0;

    scan = S_END;
  }

  if (!scan_cache->kv_iter->status ().ok ())
  {
    assert (false);
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
