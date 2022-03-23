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

#include "tran_server.hpp"

#include "communication_server_channel.hpp"
#include "disk_manager.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "server_type.hpp"
#include "system_parameter.h"

#include <cassert>
#include <cstring>
#include <functional>
#include <string>

static void assert_is_tran_server ();

tran_server::~tran_server ()
{
  assert (is_transaction_server () || m_page_server_conn_vec.empty ());
  if (is_transaction_server () && !m_page_server_conn_vec.empty ())
    {
      disconnect_page_server ();
    }
}

int
tran_server::parse_server_host (const std::string &host)
{
  std::string m_ps_hostname;
  auto col_pos = host.find (":");
  long port = -1;

  if (col_pos < 1 || col_pos >= host.length () - 1)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_HOST_PORT_PARAMETER, 2, prm_get_name (PRM_ID_PAGE_SERVER_HOSTS),
	      host.c_str ());
      return ER_HOST_PORT_PARAMETER;
    }

  try
    {
      port = std::stol (host.substr (col_pos + 1));
    }
  catch (...)
    {
    }

  if (port < 1 || port > USHRT_MAX)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_HOST_PORT_PARAMETER, 2, prm_get_name (PRM_ID_PAGE_SERVER_HOSTS),
	      host.c_str ());
      return ER_HOST_PORT_PARAMETER;
    }
  // host and port seem to be OK
  m_ps_hostname = host.substr (0, col_pos);
  er_log_debug (ARG_FILE_LINE, "Page server hosts: %s port: %d\n", m_ps_hostname.c_str (), port);

  cubcomm::node conn{port, m_ps_hostname};
  m_connection_list.push_back (conn);

  return NO_ERROR;
}

int
tran_server::parse_page_server_hosts_config (std::string &hosts)
{
  auto col_pos = hosts.find (":");

  if (col_pos < 1 || col_pos >= hosts.length () - 1)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_HOST_PORT_PARAMETER, 2, prm_get_name (PRM_ID_PAGE_SERVER_HOSTS),
	      hosts.c_str ());
      return ER_HOST_PORT_PARAMETER;
    }

  size_t pos = 0;
  std::string delimiter = ",";
  int exit_code = NO_ERROR;

  while ((pos = hosts.find (delimiter)) != std::string::npos)
    {
      std::string token = hosts.substr (0, pos);
      hosts.erase (0, pos + delimiter.length ());

      if (parse_server_host (token) != NO_ERROR)
	{
	  exit_code = ER_HOST_PORT_PARAMETER;
	}
    }
  if (parse_server_host (hosts) != NO_ERROR)
    {
      exit_code = ER_HOST_PORT_PARAMETER;
    }

  return exit_code;
}

int
tran_server::boot (const char *db_name)
{
  int error_code = init_page_server_hosts (db_name);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  if (uses_remote_storage ())
    {
      error_code = get_boot_info_from_page_server ();
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  on_boot ();

  return NO_ERROR;
}

int
tran_server::init_page_server_hosts (const char *db_name)
{
  assert_is_tran_server ();
  assert (m_page_server_conn_vec.empty ());
  /*
   * Specified behavior:
   * ===============================================================================
   * |       \    hosts config     |   empty   |    bad    |          good         |
   * |--------\--------------------|-----------|-----------|------------|----------|
   * | storage \ connections to PS |           |           |    == 0    |   > 0    |
   * |==========\==============================|===========|============|==========|
   * |   local  |                      OK      |    N/A    |     OK     |   OK     |
   * |----------|------------------------------|-----------|------------|----------|
   * |   remote |                     Error    |   Error   |   Error    |   OK     |
   * ===============================================================================
   */

  // read raw config
  //
  std::string hosts = prm_get_string_value (PRM_ID_PAGE_SERVER_HOSTS);
  bool uses_remote_storage = get_remote_storage_config ();

  // check config validity
  //
  if (!hosts.length ())
    {
      if (uses_remote_storage)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EMPTY_PAGE_SERVER_HOSTS_CONFIG, 0);
	  return ER_EMPTY_PAGE_SERVER_HOSTS_CONFIG;
	}
      else
	{
	  // no page server, local storage
	  assert (is_active_transaction_server ());
	  return NO_ERROR;
	}
    }

  int exit_code = parse_page_server_hosts_config (hosts);
  if (m_connection_list.empty ())
    {
      // no valid hosts
      int exit_code = ER_HOST_PORT_PARAMETER;
      ASSERT_ERROR_AND_SET (exit_code); // er_set was called
      return exit_code;
    }
  if (exit_code != NO_ERROR)
    {
      //there is at least one correct host in the list
      //clear the errors from parsing the bad ones
      er_clear ();
    }
  exit_code = NO_ERROR;
  // use config to connect
  //
  int valid_connection_count = 0;
  bool failed_conn = false;
  for (const cubcomm::node &node : m_connection_list)
    {
      const int local_exit_code = connect_to_page_server (node, db_name);
      if (local_exit_code == NO_ERROR)
	{
	  ++valid_connection_count;
	  exit_code = NO_ERROR;
	}
      else
	{
	  exit_code = local_exit_code;
	  failed_conn = true;
	  er_log_debug (ARG_FILE_LINE, "Failed to connect to host: %s port: %d\n", node.get_host ().c_str (), node.get_port ());
	}
    }

  if (failed_conn && valid_connection_count > 0)
    {
      //at least one valid host exists clear the error remaining from previous failing ones
      er_clear ();
    }
  // validate connections vs. config
  //
  if (valid_connection_count == 0 && uses_remote_storage)
    {
      assert (exit_code != NO_ERROR);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NO_PAGE_SERVER_CONNECTION, 0);
      exit_code = ER_NO_PAGE_SERVER_CONNECTION;
    }
  else if (valid_connection_count == 0)
    {
      // failed to connect to any page server
      assert (exit_code != NO_ERROR);
      er_clear ();
      exit_code = NO_ERROR;
    }
  er_log_debug (ARG_FILE_LINE, "Transaction server runs on %s storage.",
		uses_remote_storage ? "remote" : "local");
  return exit_code;
}

int
tran_server::get_boot_info_from_page_server ()
{
  std::string response_message;
  const int error_code = send_receive (tran_to_page_request::GET_BOOT_INFO, std::string (), response_message);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  DKNVOLS nvols_perm;
  std::memcpy (&nvols_perm, response_message.c_str (), sizeof (nvols_perm));

  disk_set_page_server_perm_volume_count (nvols_perm);

  return NO_ERROR;
}

int
tran_server::connect_to_page_server (const cubcomm::node &node, const char *db_name)
{
  auto ps_conn_error_lambda = [&node] ()
  {
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_PAGESERVER_CONNECTION, 1, node.get_host ().c_str ());
    return ER_NET_PAGESERVER_CONNECTION;
  };

  assert_is_tran_server ();

  // connect to page server
  constexpr int CHANNEL_POLL_TIMEOUT = 1000;    // 1000 milliseconds = 1 second
  cubcomm::server_channel srv_chn (db_name, SERVER_TYPE_PAGE, CHANNEL_POLL_TIMEOUT);

  srv_chn.set_channel_name ("TS_PS_comm");

  css_error_code comm_error_code = srv_chn.connect (node.get_host ().c_str (), node.get_port (),
				   CMD_SERVER_SERVER_CONNECT);
  if (comm_error_code != css_error_code::NO_ERRORS)
    {
      return ps_conn_error_lambda ();
    }

  if (srv_chn.send_int (static_cast<int> (m_conn_type)) != NO_ERRORS)
    {
      return ps_conn_error_lambda ();
    }

  int returned_code;
  if (srv_chn.recv_int (returned_code) != css_error_code::NO_ERRORS)
    {
      return ps_conn_error_lambda ();
    }
  if (returned_code != static_cast<int> (m_conn_type))
    {
      return ps_conn_error_lambda ();
    }

  er_log_debug (ARG_FILE_LINE, "Transaction server successfully connected to the page server. Channel id: %s.\n",
		srv_chn.get_channel_id ().c_str ());

  constexpr size_t RESPONSE_PARTITIONING_SIZE = 24;   // Arbitrarily chosen
  // TODO: to reduce contention as much as possible, should be equal to the maximum number
  // of active transactions that the system allows (PRM_ID_CSS_MAX_CLIENTS) + 1

  cubcomm::send_queue_error_handler no_transaction_handler { nullptr };
  // Transaction server will use message specific error handlers.
  // Implementation will assert that an error handler is present if needed.

  m_page_server_conn_vec.emplace_back (
	  new page_server_conn_t (std::move (srv_chn), get_request_handlers (), tran_to_page_request::RESPOND,
				  page_to_tran_request::RESPOND, RESPONSE_PARTITIONING_SIZE,
				  std::move (no_transaction_handler)));
  m_page_server_conn_vec.back ()->start ();

  return NO_ERROR;
}

void
tran_server::disconnect_page_server ()
{
  assert_is_tran_server ();
  const int payload = static_cast<int> (m_conn_type);
  std::string msg (reinterpret_cast<const char *> (&payload), sizeof (payload));
  er_log_debug (ARG_FILE_LINE, "Transaction server starts disconnecting from the page servers.");

  for (size_t i = 0; i < m_page_server_conn_vec.size (); i++)
    {
      er_log_debug (ARG_FILE_LINE, "Transaction server disconnected from page server with channel id: %s.\n",
		    m_page_server_conn_vec[i]->get_underlying_channel_id ().c_str ());
      m_page_server_conn_vec[i]->push (tran_to_page_request::SEND_DISCONNECT_MSG, std::move (std::string (msg)));
    }
  m_page_server_conn_vec.clear ();
  er_log_debug (ARG_FILE_LINE, "Transaction server disconnected from all page servers.");
}

bool
tran_server::is_page_server_connected () const
{
  assert_is_tran_server ();

  return !m_page_server_conn_vec.empty ();
}

bool
tran_server::uses_remote_storage () const
{
  return false;
}

void
tran_server::push_request (tran_to_page_request reqid, std::string &&payload)
{
  if (!is_page_server_connected ())
    {
      return;
    }

  m_page_server_conn_vec[0]->push (reqid, std::move (payload));
}

int
tran_server::send_receive (tran_to_page_request reqid, std::string &&payload_in, std::string &payload_out)
{
  assert (is_page_server_connected ());

  const css_error_code error_code = m_page_server_conn_vec[0]->send_recv (reqid, std::move (payload_in), payload_out);
  // NOTE: enhance error handling when:
  //  - more than one page server will be handled
  //  - fail-over will be implemented

  if (error_code != NO_ERRORS)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CONN_PAGE_SERVER_CANNOT_BE_REACHED, 0);
      er_log_debug (ARG_FILE_LINE, "Error during send_recv message to page server. Server cannot be reached.");
      return ER_CONN_PAGE_SERVER_CANNOT_BE_REACHED;
    }

  return NO_ERROR;
}

tran_server::request_handlers_map_t
tran_server::get_request_handlers ()
{
  // Insert handlers specific to all transaction servers here.
  // For now, there are no such handlers; return an empty map.
  std::map<page_to_tran_request, page_server_conn_t::incoming_request_handler_t> handlers_map;
  return handlers_map;
}

void
assert_is_tran_server ()
{
  assert (get_server_type () == SERVER_TYPE::SERVER_TYPE_TRANSACTION);
}
