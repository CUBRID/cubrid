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

#include "cubrid_getopt.h"

constexpr char *SERVER_TYPE_LONG = "type";
constexpr char SERVER_TYPE_SHORT = 't';

GETOPT_LONG server_options_map[] =
{
  {SERVER_TYPE_LONG, 1, 0, SERVER_TYPE_SHORT},
  {nullptr, 0, 0, 0}
};

constexpr int SHORT_OPTIONS_BUFSIZE = 64;
char short_options_buffer[SHORT_OPTIONS_BUFSIZE];

typedef enum
{
  UNKNOWN,
  SERVER_TYPE_TRANSACTION,
  SERVER_TYPE_PAGE,
} SERVER_TYPE;

int init_server_type (const char *db_name);
void finalize_server_type ();
SERVER_TYPE get_server_type ();
int argument_handler (int argc, char **argv, const char *database_name);
void set_server_type (SERVER_TYPE type);
bool is_tran_server_with_remote_storage ();

#endif
