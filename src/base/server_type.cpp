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
#include "passive_tran_server.hpp"
#include "communication_server_channel.hpp"
#include "connection_defs.h"
#include "error_manager.h"
#include "log_impl.h"
#include "page_server.hpp"
#include "system_parameter.h"

#include <string>

std::unique_ptr<tran_server> ts_Gl;
// non-owning "shadow" pointer of globally visible ts_Gl
passive_tran_server *pts_Gl = nullptr;

SERVER_TYPE get_server_type_from_config (server_type_config parameter_value);
transaction_server_type get_transaction_server_type_from_config (transaction_server_type_config parameter_value);
void setup_tran_server_params_on_single_node_config ();

static SERVER_TYPE g_server_type = SERVER_TYPE_UNKNOWN;
static transaction_server_type g_transaction_server_type = transaction_server_type::ACTIVE;
#if !defined(NDEBUG)
static bool g_server_type_initialized = false;
#endif

SERVER_TYPE get_server_type ()
{
  return g_server_type;
}

transaction_server_type get_transaction_server_type ()
{
  return g_transaction_server_type;
}

SERVER_TYPE get_server_type_from_config (server_type_config parameter_value)
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

transaction_server_type get_transaction_server_type_from_config (transaction_server_type_config parameter_value)
{
  switch (parameter_value)
    {
    case transaction_server_type_config::ACTIVE:
      return transaction_server_type::ACTIVE;
      break;
    case transaction_server_type_config::REPLICA:
      return transaction_server_type::PASSIVE;
      break;
    case transaction_server_type_config::STANDBY:
      return transaction_server_type::PASSIVE;
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
  const auto server_type_from_config = (server_type_config) prm_get_integer_value (PRM_ID_SERVER_TYPE);
  const auto transaction_server_type_from_config =
	  (transaction_server_type_config) prm_get_integer_value ( PRM_ID_TRANSACTION_SERVER_TYPE);
  g_transaction_server_type = get_transaction_server_type_from_config (transaction_server_type_from_config);

  if (g_server_type == SERVER_TYPE_UNKNOWN)
    {
      if (server_type_from_config == server_type_config::SINGLE_NODE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_SERVER_OPTION, 1,
		  "Single node server must have type specified as argument");
	  return ER_INVALID_SERVER_OPTION;
	}

      //if no parameter value is provided use transaction as the default type
      g_server_type = get_server_type_from_config (server_type_from_config);
    }

  if (g_server_type == SERVER_TYPE_TRANSACTION && server_type_from_config == server_type_config::SINGLE_NODE)
    {
      setup_tran_server_params_on_single_node_config ();
    }
#if !defined(NDEBUG)
  g_server_type_initialized = true;
#endif
  if (g_server_type == SERVER_TYPE_TRANSACTION)
    {
      assert (ts_Gl == nullptr);

      if (is_active_transaction_server ())
	{
	  ts_Gl.reset (new active_tran_server ());
	  log_Gl.initialize_log_prior_sender ();
	}
      else if (is_passive_transaction_server ())
	{
	  assert (pts_Gl == nullptr);

	  // passive tran server also needs (a) prior receiver(s) to receive log from page server(s)
	  log_Gl.initialize_log_prior_receiver ();

	  pts_Gl = new passive_tran_server ();
	  ts_Gl.reset (pts_Gl);
	}
      else
	{
	  assert ("neither active nor passive transaction server type" == nullptr);
	}
      er_code = ts_Gl->boot (db_name);
    }
  else if (g_server_type == SERVER_TYPE_PAGE)
    {
      // page server needs a log prior receiver
      log_Gl.initialize_log_prior_receiver ();
      log_Gl.initialize_log_prior_sender ();
    }
  else
    {
      assert ("neither transaction nor page server type" == nullptr);
    }

  if (er_code == NO_ERROR)
    {
      if (g_server_type == SERVER_TYPE_TRANSACTION)
	{
	  er_log_debug (ARG_FILE_LINE, "Starting server type: %s transaction\n",
			transaction_server_type_to_string (get_transaction_server_type ()));
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE, "Starting server type: %s\n", server_type_to_string (get_server_type ()));
	}
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
      if (is_passive_transaction_server ())
	{
	  assert (pts_Gl != nullptr);
	  pts_Gl = nullptr;
	}

      ts_Gl->disconnect_page_server ();
      if (is_active_transaction_server ())
	{
	  log_Gl.finalize_log_prior_sender ();
	}
      ts_Gl.reset (nullptr);
    }
  else if (get_server_type () == SERVER_TYPE_PAGE)
    {
      ps_Gl.disconnect_all_tran_server ();
      log_Gl.finalize_log_prior_sender ();
    }
  else
    {
      assert (get_server_type () == SERVER_TYPE_UNKNOWN);
    }
}

bool is_active_transaction_server ()
{
  return is_transaction_server () && g_transaction_server_type == transaction_server_type::ACTIVE;
}

bool is_page_server ()
{
  return g_server_type == SERVER_TYPE_PAGE;
}

bool is_passive_transaction_server ()
{
  return is_transaction_server () && g_transaction_server_type == transaction_server_type::PASSIVE;
}

bool is_passive_server ()
{
  return is_page_server () || is_passive_transaction_server ();
}

bool is_transaction_server ()
{
  return g_server_type == SERVER_TYPE_TRANSACTION;
}

bool is_tran_server_with_remote_storage ()
{
  assert (g_server_type_initialized);

  if (get_server_type () == SERVER_TYPE_TRANSACTION)
    {
      return ts_Gl->uses_remote_storage ();
    }
  return false;
}

passive_tran_server *get_passive_tran_server_ptr ()
{
  if (is_passive_transaction_server ())
    {
      assert (pts_Gl != nullptr);
      return pts_Gl;
    }
  else
    {
      assert (is_passive_transaction_server ());
      return nullptr;
    }
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

bool is_active_transaction_server ()
{
  return true;
}

bool is_page_server ()
{
  return false;
}

bool is_passive_transaction_server ()
{
  return false;
}

bool is_passive_server ()
{
  return false;
}

bool is_transaction_server ()
{
  return true;
}

#endif // !SERVER_MODE = SA_MODE
