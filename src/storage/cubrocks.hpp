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

#ifndef _CUBROCKS_HPP_
#define _CUBROCKS_HPP_

#include "dbtype_def.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/table.h"
#include "rocksdb/utilities/transaction.h"
#include "rocksdb/utilities/transaction_db.h"

namespace cubrocks
{
  void kv_version (void);
  std::string kv_postfix (char *path);

  class context
  {
    public:
      context ();

      void kv_config ();

      /* basic */
      bool kv_create (std::string path);
      bool kv_open (std::string path);
      bool kv_destroy (std::string path);

      bool is_alive ();

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
      bool alive;
  };

  extern context *ctx;
}

#endif // _CUBROCKS_HPP_
