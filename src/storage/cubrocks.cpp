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
#include <iostream>
#include <assert.h>

#include "rocksdb/version.h"
#include "rocksdb/slice_transform.h"

#include "cubrocks.hpp"

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
  kv_config ();

  alive = false;
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
  opt.cf.prefix_extractor.reset (rocksdb::NewCappedPrefixTransform (4));

  opt.cf_descriptor.push_back (rocksdb::ColumnFamilyDescriptor ("default", opt.cf));
}

bool
cubrocks::context::is_alive (void)
{
  return alive;
}

bool
cubrocks::context::kv_create (std::string path)
{
  assert (!alive);

  opt.db.create_if_missing = true;
  opt.db.error_if_exists = true;

  /* db will be closed in context::close ( ... ) that is called from boot_.._finalize */
  alive = rocksdb::TransactionDB::Open (opt.db, opt.txndb, path, opt.cf_descriptor, &opt.cf_handles, &db).ok();
  return alive;
}

bool
cubrocks::context::kv_open (std::string path)
{
  assert (!alive);

  opt.db.create_if_missing = false;
  opt.db.error_if_exists = false;

  alive = rocksdb::TransactionDB::Open (opt.db, opt.txndb, path, opt.cf_descriptor, &opt.cf_handles, &db).ok();
  return alive;
}

bool
cubrocks::context::kv_close ()
{
  /* it is not clear whether "Flush -> WaitForCompact" should be called in that order.
   * also, check if DestroyColumnFamilyHandle should be called. */
  assert (alive);

  rocksdb::WaitForCompactOptions opt_compact;
  rocksdb::FlushOptions opt_flush;

  opt_flush.wait = true;
  if (!db->Flush (opt_flush, opt.cf_handles).ok ())
    {
      return false;
    }

  opt_compact.close_db = true;
  if (!db->WaitForCompact (opt_compact).ok ())
    {
      return false;
    }

  delete db;

  alive = false;

  return true;
}

bool
cubrocks::context::kv_destroy (std::string path)
{
  assert (!alive);

  rocksdb::Options options;

  return rocksdb::DestroyDB (path, options).ok();
}
