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
#include "log_impl.h"
#include "page_server.hpp"
#include "system_parameter.h"

#include <string>

SERVER_TYPE get_value_from_config (server_type_config parameter_value);
void setup_tran_server_params_on_single_node_config ();

static SERVER_TYPE g_server_type = SERVER_TYPE_UNKNOWN;
#if !defined(NDEBUG)
static bool g_server_type_initialized = false;
#endif

SERVER_TYPE get_server_type ()
{
  return g_server_type;
}

SERVER_TYPE get_value_from_config (server_type_config parameter_value)
{
  switch (parameter_value)
    {
    case server_type_config::TRANSACTION:
      return SERVER_TYPE_TRANSACTION;
      break;
    case server_type_config::PAGE:
      return SERVER_TYPE_PAGE;
      break;
    default:
      assert (false);
    }
}

void set_server_type (SERVER_TYPE type)
{
  g_server_type = type;
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

int init_server_type (const char *db_name)
{
  int er_code = NO_ERROR;
  server_type_config parameter_value = (server_type_config) prm_get_integer_value (PRM_ID_SERVER_TYPE);
  if (g_server_type == SERVER_TYPE_UNKNOWN)
    {
      if (parameter_value == server_type_config::SINGLE_NODE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_SERVER_OPTION, 1,
		  "Single node server must have type specified as argument");
	  return ER_INVALID_SERVER_OPTION;
	}

      //if no parameter value is provided use transaction as the default type
      g_server_type = get_value_from_config (parameter_value);
    }

  if (g_server_type == SERVER_TYPE_TRANSACTION || parameter_value == server_type_config::SINGLE_NODE)
    {
      setup_tran_server_params_on_single_node_config ();
    }
#if !defined(NDEBUG)
  g_server_type_initialized = true;
#endif
  if (g_server_type == SERVER_TYPE_TRANSACTION)
    {
      er_code = ats_Gl.init_page_server_hosts (db_name);
    }
  else
    {
      assert (g_server_type == SERVER_TYPE_PAGE);

      // page server needs a log prior receiver
      log_Gl.initialize_log_prior_receiver ();
    }

  if (er_code == NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "Starting server type: %s\n",
		    get_server_type () == SERVER_TYPE_PAGE ? "page" : "transaction");
    }
  else
    {
      ASSERT_ERROR ();
    }
  return er_code;
}

void setup_tran_server_params_on_single_node_config ()
{
  char *page_hosts_new_value;
  constexpr size_t PAGE_HOSTS_BUFSIZE = 32;
  page_hosts_new_value = (char *) malloc (PAGE_HOSTS_BUFSIZE); // free is called by sysprm_final()

  sprintf (page_hosts_new_value, "localhost:%d", prm_get_master_port_id ());
  prm_set_string_value (PRM_ID_PAGE_SERVER_HOSTS, page_hosts_new_value);
  prm_set_bool_value (PRM_ID_REMOTE_STORAGE, true);
}

void finalize_server_type ()
{
  if (get_server_type () == SERVER_TYPE_TRANSACTION)
    {
      ats_Gl.disconnect_page_server ();
    }
  else if (get_server_type() == SERVER_TYPE_PAGE)
    {
      ps_Gl.disconnect_active_tran_server ();
    }
  else
    {
      assert (get_server_type () == SERVER_TYPE_UNKNOWN);
    }
}

bool is_tran_server_with_remote_storage ()
{
  assert (g_server_type_initialized);

  if (get_server_type () == SERVER_TYPE_TRANSACTION)
    {
      return ats_Gl.uses_remote_storage ();
    }
  return false;
}

#else // !SERVER_MODE = SA_MODE

int init_server_type (const char *)
{
  int err_code = NO_ERROR;

  g_server_type = SERVER_TYPE_TRANSACTION;
#if !defined(NDEBUG)
  g_server_type_initialized = true;
#endif

  const bool uses_remote_storage = prm_get_bool_value (PRM_ID_REMOTE_STORAGE);
  if (uses_remote_storage)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TOOL_INVALID_WITH_REMOTE_STORAGE, 1, "Stand-alone mode");
      err_code = ER_TOOL_INVALID_WITH_REMOTE_STORAGE;
    }

  return err_code;
}

void finalize_server_type ()
{
}

bool is_tran_server_with_remote_storage ()
{
  return false;
}

#endif // !SERVER_MODE = SA_MODE
