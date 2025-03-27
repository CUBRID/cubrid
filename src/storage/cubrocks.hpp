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
 * cubrocks.hpp - rocksdb for cubrid
 */

#ifndef _CUBROCKS_HPP_
#define _CUBROCKS_HPP_

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/table.h"
#include "rocksdb/utilities/transaction.h"
#include "rocksdb/utilities/transaction_db.h"

#include "dbtype_def.h"
#include "heap_file.h"

#define is_user_class_oid(oid) \
  ((oid)->volid == 0 && (((oid)->pageid == 210 && (oid)->slotid >= 2) || ((oid)->pageid > 210 && (oid)->pageid <= 255)))

#define is_user_oid(oid) ((oid)->volid == 10)

namespace cubrocks
{
  struct kv_transaction
  {
    /* virtual OID */
    UINT64 reserved_next;
    UINT64 reserved_count;

    rocksdb::Transaction *txn;
  };

  void kv_version (void);
  std::string kv_postfix (char *path);

  class context
  {
    public:
      context ();
      ~context ();

      void kv_config ();

      /* ================================================================== */
      /* debug                                                              */
      /* ================================================================== */

      bool is_alive ();
      bool is_tran_started (int tran_index);

      /* ================================================================== */
      /* virtual (for key)                                                  */
      /* ================================================================== */

      /* counter for oid */
      void kv_reserve_void (int tran_index, int count);
      OID kv_get_void (int tran_index);

      /* ================================================================== */
      /* transaction                                                        */
      /* ================================================================== */

      /* use default value for trid when there is no information about trid */
      void kv_tran_start (int tran_index, int trid = 0);

      void kv_tran_commit (int tran_index);
      void kv_tran_abort (int tran_index);

      /* ================================================================== */
      /* operation                                                          */
      /* ================================================================== */

      /* insert, update, delete */
      int kv_logical_write (int tran_index, HEAP_OPERATION_CONTEXT *context);

      /* scan */
      void kv_scan_start (HEAP_SCANCACHE *scan_cache);
      void kv_scan_end (HEAP_SCANCACHE *scan_cache);
      SCAN_CODE kv_logical_scan (int tran_index, OID *class_oid, OID *next_oid, RECDES *recdes, HEAP_SCANCACHE *scan_cache,
				 int ispeeking);

      SCAN_CODE kv_lock_and_get (int tran_index, OID *class_oid, OID *oid, RECDES *recdes, HEAP_SCANCACHE *scan_cache, int ispeeking);
      void kv_lock_release (int tran_index, OID *class_oid, OID *oid);

      /* ================================================================== */
      /* basic                                                              */
      /* ================================================================== */

      void kv_create (std::string path);
      void kv_open (std::string path);
      void kv_close ();
      void kv_destroy (std::string path);

    private:
      struct
      {
	rocksdb::DBOptions db;
	rocksdb::TransactionDBOptions txndb;
	rocksdb::ColumnFamilyOptions cf;
	rocksdb::BlockBasedTableOptions table;
	std::vector<rocksdb::ColumnFamilyDescriptor> cf_descriptor;
	std::vector<rocksdb::ColumnFamilyHandle *> cf_handles;
      } opt;

      rocksdb::TransactionDB *db;
      kv_transaction *transactions;

      bool alive;

      UINT64 virtual_counter;

      void kv_store_void ();
      void kv_restore_void ();

      bool transactions_initialize ();
      void transactions_finalize ();
  };

  extern context *ctx;
}

#endif // _CUBROCKS_HPP_
