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

#ifndef _SERVER_TYPE_H_
#define _SERVER_TYPE_H_

#include "server_type_enum.hpp"

#include <memory>

/* forward declarations
 */
class tran_server;
class active_tran_server;
class passive_tran_server;

bool is_active_transaction_server ();
bool is_page_server ();
bool is_passive_transaction_server ();
bool is_passive_server ();
bool is_transaction_server ();
bool is_tran_server_with_remote_storage ();
SERVER_TYPE get_server_type ();
transaction_server_type get_transaction_server_type ();
void set_server_type (SERVER_TYPE type);
void finalize_server_type ();
int init_server_type (const char *db_name);

active_tran_server *get_active_tran_server_ptr ();
passive_tran_server *get_passive_tran_server_ptr ();

extern std::unique_ptr<tran_server> ts_Gl;

#endif // _SERVER_TYPE_H_
