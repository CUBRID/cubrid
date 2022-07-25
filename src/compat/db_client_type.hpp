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
 * db_client_type.hpp -  Definitions for client types
 */

#ifndef _DB_CLIENT_TYPE_HPP
#define _DB_CLIENT_TYPE_HPP

enum db_client_type
{
  DB_CLIENT_TYPE_UNKNOWN = -1,
  DB_CLIENT_TYPE_SYSTEM_INTERNAL = 0,

  DB_CLIENT_TYPE_DEFAULT = 1,
  DB_CLIENT_TYPE_CSQL = 2,
  DB_CLIENT_TYPE_READ_ONLY_CSQL = 3,
  DB_CLIENT_TYPE_BROKER = 4,
  DB_CLIENT_TYPE_READ_ONLY_BROKER = 5,
  DB_CLIENT_TYPE_SLAVE_ONLY_BROKER = 6,
  DB_CLIENT_TYPE_ADMIN_UTILITY = 7,
  DB_CLIENT_TYPE_ADMIN_CSQL = 8,
  DB_CLIENT_TYPE_LOG_COPIER = 9,
  DB_CLIENT_TYPE_LOG_APPLIER = 10,
  DB_CLIENT_TYPE_RW_BROKER_REPLICA_ONLY = 11,
  DB_CLIENT_TYPE_RO_BROKER_REPLICA_ONLY = 12,
  DB_CLIENT_TYPE_SO_BROKER_REPLICA_ONLY = 13,
  DB_CLIENT_TYPE_ADMIN_CSQL_WOS = 14,	/* admin csql that can write on standby */
  DB_CLIENT_TYPE_SKIP_VACUUM_CSQL = 15,
  DB_CLIENT_TYPE_SKIP_VACUUM_ADMIN_CSQL = 16,
  DB_CLIENT_TYPE_ADMIN_COMPACTDB_WOS = 17, /* admin compactdb that can run on standby */
  DB_CLIENT_TYPE_ADMIN_LOADDB_COMPAT = 18, /* loaddb with --no-user-specified-name option */

  DB_CLIENT_TYPE_MAX
};

#endif /* _DB_CLIENT_TYPE_HPP */
