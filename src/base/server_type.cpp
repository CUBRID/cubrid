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

#include "server_type.hpp"

#include "active_tran_server.hpp"
#include "passive_tran_server.hpp"
#include "communication_server_channel.hpp"
#include "connection_defs.h"
#include "error_manager.h"
#include "log_impl.h"
#include "page_server.hpp"
#include "system_parameter.h"
#include "tcp.h"

#include <string>

std::unique_ptr<tran_server> ts_Gl;

// non-owning "shadow" pointer of globally visible ts_Gl
active_tran_server *ats_Gl = nullptr;
passive_tran_server *pts_Gl = nullptr;

SERVER_TYPE get_server_type_from_config (server_type_config parameter_value);
transaction_server_type get_transaction_server_type_from_config (transaction_server_type_config parameter_value);
void setup_tran_server_params_on_single_node_config ();
int setup_tran_server_params_on_ha_mode ();
bool is_localhost (const char *hostname);

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

std::unique_ptr<page_server> ps_Gl;

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

  // if ha_mode, then set page_server_hosts, remote_storage, and transaction_server_type
  if (g_server_type == SERVER_TYPE_TRANSACTION)
    {
      if (!HA_DISABLED ())
	{
	  assert (server_type_from_config != server_type_config::SINGLE_NODE);
	  er_code = setup_tran_server_params_on_ha_mode ();
	}
      else if (server_type_from_config == server_type_config::SINGLE_NODE)
	{
	  setup_tran_server_params_on_single_node_config ();
	}
    }

#if !defined(NDEBUG)
  g_server_type_initialized = true;
#endif
  if (g_server_type == SERVER_TYPE_TRANSACTION)
    {
      assert (ts_Gl == nullptr);

      if (is_active_transaction_server ())
	{
	  assert (ats_Gl == nullptr);
	  ats_Gl = new active_tran_server ();
	  ts_Gl.reset (ats_Gl);
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
      ps_Gl.reset (new page_server());
      // page server needs a log prior receiver
      log_Gl.initialize_log_prior_receiver ();
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

int setup_tran_server_params_on_ha_mode ()
{
  char *page_server_host_list = NULL;
  constexpr size_t MAX_BUFSIZE = 4096;
  int list_size = 0;

  char ha_node_list[MAX_BUFSIZE];
  char *str, *savep;

  int port_id = prm_get_master_port_id ();

  const char *localhost_str = "localhost";

  page_server_host_list = (char *) calloc (MAX_BUFSIZE, sizeof (char)); // free is called by sysprm_final()
  if (page_server_host_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, MAX_BUFSIZE);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  list_size = MAX_BUFSIZE;

  strncpy_bufsize (ha_node_list, prm_get_string_value (PRM_ID_HA_NODE_LIST));

  str = strtok_r (ha_node_list, "@", &savep); // dbname@host1:host2:...
  str = strtok_r (NULL, ",:", &savep);
  while (str)
    {
      char page_server_host[MAX_BUFSIZE] = {0};

      if (is_localhost (str))
	{
	  sprintf (page_server_host, "%s:%d,", localhost_str, port_id);
	}
      else
	{
	  sprintf (page_server_host, "%s:%d,", str, port_id);
	}

      if (strlen (page_server_host) + strlen (page_server_host_list) >= list_size)
	{
	  /* Block the overflow */
	  char *tmp = (char *) realloc (page_server_host_list, list_size + MAX_BUFSIZE);
	  if (tmp == NULL)
	    {
	      free_and_init (page_server_host_list);

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, list_size + MAX_BUFSIZE);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  page_server_host_list = tmp;
	  list_size += MAX_BUFSIZE;
	}

      strcat (page_server_host_list, page_server_host);
      str = strtok_r (NULL, ",:", &savep);
    }

  /* Remove the comma at the end of the page_server_host_list, (eg : "host1:port,host2:port,host3:port,") */
  page_server_host_list[strlen (page_server_host_list) - 1] = '\0';

  prm_set_string_value (PRM_ID_PAGE_SERVER_HOSTS, page_server_host_list);
  prm_set_bool_value (PRM_ID_REMOTE_STORAGE, true);

  /* TODO:
   * *transaction_server_type* has to be determined.
   * To determine the transaction_server_type, we need to know the node's state,
   * and to know the node's state, we need to query the cub_master for the node's state.
   * Therefore, in order to determine the transaction_server_type,
   * communication with the cub_master must precede the init_server_type() step.
   */

  return NO_ERROR;
}

bool is_localhost (const char *hostname)
{
  assert (hostname != NULL);
  char local_hostname[CUB_MAXHOSTNAMELEN];

  if (GETHOSTNAME (local_hostname, CUB_MAXHOSTNAMELEN) != 0)
    {
      return false;
    }

  if (strcmp (hostname, local_hostname) == 0)
    {
      return true;
    }
  else
    {
      return false;
    }
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
      else
	{
	  assert (ats_Gl != nullptr);
	  ats_Gl = nullptr;
	}
      ts_Gl.reset (nullptr);
    }
  else if (get_server_type () == SERVER_TYPE_PAGE)
    {
      ps_Gl.reset (nullptr);
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

active_tran_server *get_active_tran_server_ptr ()
{
  if (is_active_transaction_server ())
    {
      assert (ats_Gl != nullptr);
      return ats_Gl;
    }
  else
    {
      assert (false);
      return nullptr;
    }
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
      assert (false);
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
