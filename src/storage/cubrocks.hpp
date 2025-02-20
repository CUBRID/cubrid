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

//#define KV_TRANSACTION_

namespace cubrocks
{
  struct kv_transaction
  {
    bool active;
    rocksdb::Transaction *txn;
  };

  void kv_version (void);
  std::string kv_postfix (char *path);

  class context
  {
    public:
      context ();

      void kv_config ();

      /* for debug */
      bool is_alive ();
      bool is_tran_active (int tran_index);
      bool is_tran_started (int tran_index);

      /* transaction */
      void kv_tran_activate (int tran_index);
      void kv_tran_deactivate (int tran_index);

      bool kv_tran_start (int tran_index);

      bool kv_tran_commit (int tran_index);
      bool kv_tran_abort (int tran_index);

      /* basic */
      bool kv_create (std::string path);
      bool kv_open (std::string path);
      bool kv_close ();
      bool kv_destroy (std::string path);

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

      bool transactions_initialize ();
  };

  extern context *ctx;
}

#endif // _CUBROCKS_HPP_
