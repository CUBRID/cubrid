/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2021 CUBRID Corporation
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

#ifndef _SERVER_TYPE_H_
#define _SERVER_TYPE_H_

#include "server_type_enum.hpp"

int init_server_type (const char *db_name);
void finalize_server_type ();
SERVER_TYPE get_server_type ();
SERVER_TYPE get_value_from_config (server_type_config parameter_value);
bool is_tran_server_with_remote_storage ();
void set_server_type (SERVER_TYPE type);
void setup_tran_server_params_on_single_node_config ();

#endif // _SERVER_TYPE_H_
