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

#include "communication_server_channel.hpp"
#include "connection_defs.h"
#include "error_manager.h"
#include "server_support.h"
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
static std::string g_pageserver_hostname;
static int g_pageserver_port;

void init_page_server_hosts (const char *db_name);
void connect_to_pageserver (std::string host, int port, const char *db_name);

void init_server_type (const char *db_name)
{
  g_server_type = (SERVER_TYPE) prm_get_integer_value (PRM_ID_SERVER_TYPE);
  if (g_server_type == SERVER_TYPE_TRANSACTION)
    {
      init_page_server_hosts (db_name);
    }
}

void init_page_server_hosts (const char *db_name)
{
  assert (g_server_type == SERVER_TYPE_TRANSACTION);
  std::string hosts = prm_get_string_value (PRM_ID_PAGE_SERVER_HOSTS);

  if (!hosts.length ())
    {
      return;
    }

  auto col_pos = hosts.find (":");

  if (col_pos < 1 || col_pos >= hosts.length () - 1)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_HOST_PORT_PARAMETER, 2, prm_get_name (PRM_ID_PAGE_SERVER_HOSTS),
	      hosts.c_str ());
      return;
    }

  long port = -1;
  try
    {
      port = std::stol (hosts.substr (col_pos+1));
    }
  catch (...)
    {
    }

  if (port < 1 || port > USHRT_MAX)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_HOST_PORT_PARAMETER, 2, prm_get_name (PRM_ID_PAGE_SERVER_HOSTS),
	      hosts.c_str ());
      return;
    }
  g_pageserver_port = port;

  // host and port seem to be OK
  g_pageserver_hostname = hosts.substr (0, col_pos);
  er_log_debug (ARG_FILE_LINE, "Page server hosts: %s port: %d\n", g_pageserver_hostname.c_str (), g_pageserver_port);

  connect_to_pageserver (g_pageserver_hostname, g_pageserver_port, db_name);
}

void connect_to_pageserver (std::string host, int port, const char *db_name)
{
  assert (get_server_type () == SERVER_TYPE_TRANSACTION);

  // connect to page server
  cubcomm::server_channel srv_chn (db_name);

  srv_chn.set_channel_name ("ATS_PS_comm");

  css_error_code comm_error_code = srv_chn.connect (host.c_str (), port, CMD_SERVER_SERVER_CONNECT);
  if (comm_error_code != css_error_code::NO_ERRORS)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_PAGESERVER_CONNECTION, 1, host.c_str());
      return;
    }

  if (!srv_chn.send_int (static_cast <int> (cubcomm::server_server::CONNECT_TRANSACTION_SERVER)))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_PAGESERVER_CONNECTION, 1, host.c_str());
      return;
    }
}

#else // !SERVER_MODE = SA_MODE

void init_server_type (const char *)
{
  g_server_type = SERVER_TYPE_TRANSACTION;
}

#endif // !SERVER_MODE = SA_MODE