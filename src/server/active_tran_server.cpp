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

#include "active_tran_server.hpp"

#include "communication_server_channel.hpp"
#include "error_manager.h"
#include "log_impl.h"
#include "log_lsa.hpp"
#include "log_prior_send.hpp"
#include "memory_alloc.h"
#include "server_type.hpp"
#include "system_parameter.h"

#include <cassert>
#include <cstring>
#include <functional>
#include <string>

active_tran_server ats_Gl;

static void assert_is_active_tran_server ();

active_tran_server::~active_tran_server ()
{
  if (get_server_type () == SERVER_TYPE_TRANSACTION && is_page_server_connected ())
    {
      disconnect_page_server ();
    }
  else
    {
      assert (m_page_server_conn_vec.empty ());
    }
}

int
active_tran_server::parse_server_host (const std::string &host)
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
active_tran_server::parse_page_server_hosts_config (std::string &hosts)
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
active_tran_server::init_page_server_hosts (const char *db_name)
{
  assert_is_active_tran_server ();
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
  m_uses_remote_storage = prm_get_bool_value (PRM_ID_REMOTE_STORAGE);

  // check config validity
  //
  if (!hosts.length ())
    {
      if (m_uses_remote_storage)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EMPTY_PAGE_SERVER_HOSTS_CONFIG, 0);
	  return ER_EMPTY_PAGE_SERVER_HOSTS_CONFIG;
	}
      else
	{
	  // no page server, local storage
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
  if (valid_connection_count == 0 && m_uses_remote_storage)
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
		m_uses_remote_storage ? "remote" : "local");

  // failed to connect to any page server
  assert (valid_connection_count > 0 || !m_uses_remote_storage);
  return exit_code;
}

int
active_tran_server::connect_to_page_server (const cubcomm::node &node, const char *db_name)
{
  assert_is_active_tran_server ();

  // connect to page server
  constexpr int CHANNEL_POLL_TIMEOUT = 1000;    // 1000 milliseconds = 1 second
  cubcomm::server_channel srv_chn (db_name, CHANNEL_POLL_TIMEOUT);

  srv_chn.set_channel_name ("ATS_PS_comm");

  css_error_code comm_error_code = srv_chn.connect (node.get_host ().c_str (), node.get_port (),
				   CMD_SERVER_SERVER_CONNECT);
  if (comm_error_code != css_error_code::NO_ERRORS)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_PAGESERVER_CONNECTION, 1, node.get_host ().c_str ());
      return ER_NET_PAGESERVER_CONNECTION;
    }

  if (!srv_chn.send_int (static_cast<int> (cubcomm::server_server::CONNECT_ACTIVE_TRAN_TO_PAGE_SERVER)))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_PAGESERVER_CONNECTION, 1, node.get_host ().c_str ());
      return ER_NET_PAGESERVER_CONNECTION;
    }

  er_log_debug (ARG_FILE_LINE, "Transaction server successfully connected to the page server. Channel id: %s.\n",
		srv_chn.get_channel_id ().c_str ());

  m_page_server_conn_vec.emplace_back (new page_server_conn_t (std::move (srv_chn),
  {
    {
      ps_to_ats_request::SEND_SAVED_LSA,
      std::bind (&active_tran_server::receive_saved_lsa, std::ref (*this), std::placeholders::_1)
    },
    {
      ps_to_ats_request::SEND_LOG_PAGE,
      std::bind (&active_tran_server::receive_log_page, std::ref (*this), std::placeholders::_1)
    },
    {
      ps_to_ats_request::SEND_DATA_PAGE,
      std::bind (&active_tran_server::receive_data_page, std::ref (*this), std::placeholders::_1)
    }
  }));

  log_Gl.m_prior_sender.add_sink (std::bind (&active_tran_server::push_request, std::ref (*this),
				  ats_to_ps_request::SEND_LOG_PRIOR_LIST, std::placeholders::_1));

  return NO_ERROR;
}

void
active_tran_server::disconnect_page_server ()
{
  assert_is_active_tran_server ();

  m_page_server_conn_vec.clear ();
}

bool
active_tran_server::is_page_server_connected () const
{
  assert_is_active_tran_server ();

  return !m_page_server_conn_vec.empty ();
}

void
active_tran_server::init_page_brokers ()
{
  m_log_page_broker.reset (new page_broker<log_page_type> ());
  m_data_page_broker.reset (new page_broker<data_page_type> ());
}

void
active_tran_server::finalize_page_brokers ()
{
  m_log_page_broker.reset ();
  m_data_page_broker.reset ();
}

page_broker<log_page_type> &
active_tran_server::get_log_page_broker ()
{
  assert (m_log_page_broker);
  return *m_log_page_broker;
}

page_broker<data_page_type> &
active_tran_server::get_data_page_broker ()
{
  assert (m_data_page_broker);
  return *m_data_page_broker;
}

bool
active_tran_server::uses_remote_storage () const
{
  assert_is_active_tran_server ();

  return m_uses_remote_storage;
}

void
active_tran_server::push_request (ats_to_ps_request reqid, std::string &&payload)
{
  if (!is_page_server_connected ())
    {
      return;
    }

  m_page_server_conn_vec[0]->push (reqid, std::move (payload));
}

void
active_tran_server::receive_log_page (cubpacking::unpacker &upk)
{
  std::string message;
  upk.unpack_string (message);

  int error_code;
  std::memcpy (&error_code, message.c_str (), sizeof (error_code));

  if (error_code == NO_ERROR)
    {
      auto shared_log_page = std::make_shared<log_page_owner> (message.c_str () + sizeof (error_code));

      if (prm_get_bool_value (PRM_ID_ER_LOG_READ_LOG_PAGE))
	{
	  _er_log_debug (ARG_FILE_LINE, "Received log page message from Page Server. Page ID: %lld\n",
			 shared_log_page->get_id ());
	}
      m_log_page_broker->set_page (shared_log_page->get_id (), std::move (shared_log_page));
    }
  else
    {
      if (prm_get_bool_value (PRM_ID_ER_LOG_READ_LOG_PAGE))
	{
	  _er_log_debug (ARG_FILE_LINE, "Received log page message from Page Server. Error code: %d\n", error_code);
	}
    }
}

void active_tran_server::receive_data_page (cubpacking::unpacker &upk)
{
  std::string message;
  upk.unpack_string (message);

  // The message is either an error code or the content of the data page
  if (message.size () == sizeof (int))
    {
      // We have an error.
      int error_code;
      std::memcpy (&error_code, message.c_str (), sizeof (error_code));

      if (prm_get_bool_value (PRM_ID_ER_LOG_READ_DATA_PAGE))
	{
	  _er_log_debug (ARG_FILE_LINE, "Received data page message from Page Server. Error: %d \n", error_code);
	}
    }
  else
    {
      assert (message.size () == db_io_page_size ());
      // We have a page.
      auto shared_data_page = std::make_shared<std::string> (std::move (message));

      auto io_page = reinterpret_cast<const FILEIO_PAGE *> (shared_data_page->c_str ());
      VPID id;
      id.pageid = io_page->prv.pageid;
      id.volid = io_page->prv.volid;

      m_data_page_broker->set_page (id, std::move (shared_data_page));

      if (prm_get_bool_value (PRM_ID_ER_LOG_READ_DATA_PAGE))
	{
	  _er_log_debug (ARG_FILE_LINE, "Received data page message from Page Server. LSA: %lld|%d, Page ID: %ld, Volid: %d",
			 LSA_AS_ARGS (&io_page->prv.lsa), io_page->prv.pageid, io_page->prv.volid);
	}
    }
}

void active_tran_server::receive_saved_lsa (cubpacking::unpacker &upk)
{
  std::string message;
  log_lsa saved_lsa;

  upk.unpack_string (message);
  assert (sizeof (log_lsa) == message.size ());
  std::memcpy (&saved_lsa, message.c_str (), sizeof (log_lsa));

  if (log_Gl.m_max_ps_flushed_lsa < saved_lsa)
    {
      log_Gl.update_max_ps_flushed_lsa (saved_lsa);
    }

  if (prm_get_bool_value (PRM_ID_ER_LOG_COMMIT_CONFIRM))
    {
      _er_log_debug (ARG_FILE_LINE, "[COMMIT CONFIRM] Received LSA = %lld|%d.\n", LSA_AS_ARGS (&saved_lsa));
    }
}

void
assert_is_active_tran_server ()
{
  assert (get_server_type () == SERVER_TYPE::SERVER_TYPE_TRANSACTION);
}
