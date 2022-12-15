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

#ifndef _SERVER_TYPE_ENUM_H_
#define _SERVER_TYPE_ENUM_H_
#include <assert.h>

typedef enum
{
  SERVER_TYPE_UNKNOWN,
  SERVER_TYPE_TRANSACTION,
  SERVER_TYPE_PAGE,
  SERVER_TYPE_ANY,
} SERVER_TYPE;

enum class server_type_config
{
  TRANSACTION,
  PAGE,
  SINGLE_NODE,
};

enum class transaction_server_type
{
  ACTIVE,
  PASSIVE,
};

enum class transaction_server_type_config
{
  ACTIVE,
  REPLICA,
  STANDBY,
};

constexpr const char *
server_type_to_string (SERVER_TYPE type)
{
  switch (type)
    {
    case SERVER_TYPE_UNKNOWN:
      return "unknown";
    case SERVER_TYPE_TRANSACTION:
      return "transaction";
    case SERVER_TYPE_PAGE:
      return "page";
    case SERVER_TYPE_ANY:
      return "any";
    default:
      assert (false);
    }
}

constexpr const char *
server_type_config_to_string (server_type_config type)
{
  switch (type)
    {
    case server_type_config::PAGE:
      return "page";
    case server_type_config::TRANSACTION:
      return "transaction";
    case server_type_config::SINGLE_NODE:
      return "single_node";
    default:
      assert (false);
    }
}

constexpr const char *
transaction_server_type_to_string (transaction_server_type type)
{
  switch (type)
    {
    case transaction_server_type::ACTIVE:
      return "active";
    case transaction_server_type::PASSIVE:
      return "passive";
    default:
      assert (false);
    }
}

constexpr const char *
transaction_server_type_config_to_string (transaction_server_type_config type)
{
  switch (type)
    {
    case transaction_server_type_config::ACTIVE:
      return "active";
    case transaction_server_type_config::REPLICA:
      return "replica";
    case transaction_server_type_config::STANDBY:
      return "standby";
    default:
      assert (false);
    }
}
#endif // _SERVER_TYPE_ENUM_H_
