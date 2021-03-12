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

#include "server_type.hpp"

#include "active_tran_server.hpp"
#include "communication_server_channel.hpp"
#include "connection_defs.h"
#include "error_manager.h"
#include "page_server.hpp"
#include "system_parameter.h"

#include <string>

static SERVER_TYPE g_server_type;

SERVER_TYPE get_server_type ()
{
  return g_server_type;
}

// SERVER_MODE & SA_MODE have completely different behaviors.
//
// SERVER_MODE allows both transaction & page server types. SERVER_MODE transaction server communicates with the
// SERVER_MODE page server.
//
// SA_MODE is considered a transaction server, but it is different from SERVER_MODE transaction type. It does not
// communicate with the page server. The behavior needs further consideration and may be changed.
//

#if defined (SERVER_MODE)

void init_server_type (const char *db_name)
{
  g_server_type = (SERVER_TYPE) prm_get_integer_value (PRM_ID_SERVER_TYPE);
  if (g_server_type == SERVER_TYPE_TRANSACTION)
    {
      ats_Gl.init_page_server_hosts (db_name);
    }
}

void final_server_type ()
{
  if (get_server_type () == SERVER_TYPE_TRANSACTION)
    {
      ats_Gl.disconnect_page_server ();
    }
  else
    {
      assert (get_server_type () == SERVER_TYPE_PAGE);
      ps_Gl.disconnect_active_tran_server ();
    }
}

#else // !SERVER_MODE = SA_MODE

void init_server_type (const char *)
{
  g_server_type = SERVER_TYPE_TRANSACTION;
}

void final_server_type ()
{
}

#endif // !SERVER_MODE = SA_MODE
