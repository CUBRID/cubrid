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

#ifndef _TRAN_SERVER_HPP_
#define _TRAN_SERVER_HPP_

#include "communication_node.hpp"
#include "communication_server_channel.hpp"
#include "request_sync_client_server.hpp"
#include "tran_page_requests.hpp"
#include "log_lsa.hpp"

#include <string>
#include <vector>

// forward declaration
namespace cubpacking
{
  class unpacker;
}

// Transaction server class hierarchies:
//
//                              tran_server
//                               /      |
//                              /       |
//                             /        |
//             active_tran_server     passive_tran_server
//
//
// tran_server:
//   the base class that defines the common public interface for the transaction servers. The common parts are:
//      - Connection management
//      - Page brokers
//      - Booting
//
// active_tran_server:
//   class derived from tran_server that is specific only to active transaction server.
//
// passive_tran_server:
//   class derived from tran_server that is specific only to passive transaction server.
//
class tran_server
{
  public:
    tran_server () = delete;
    tran_server (cubcomm::server_server conn_type)
      : m_conn_type (conn_type)
    {
    }
    tran_server (const tran_server &) = delete;
    tran_server (tran_server &&) = delete;

    virtual ~tran_server ();

    tran_server &operator = (const tran_server &) = delete;
    tran_server &operator = (tran_server &&) = delete;

    int boot (const char *db_name);

    /* send request to the main connection */
    void push_request (tran_to_page_request reqid, std::string &&payload);
    int send_receive (tran_to_page_request reqid, std::string &&payload_in, std::string &payload_out) const;

    void disconnect_all_page_servers ();
    bool is_page_server_connected () const;
    virtual bool uses_remote_storage () const;

    // Before disconnecting page server, make sure no message is being sent anymore to the page server.
    virtual void stop_outgoing_page_server_messages () = 0;

  protected:
    /*
     * The permanent data of each page server is stored and served regardless of connection.
     */
    class page_server_node
    {
      public:
	page_server_node (const cubcomm::node &&conn_node)
	  : m_conn_node { conn_node }
	  , m_saved_lsa { NULL_LSA }
	{ }
	page_server_node () = delete;

	page_server_node (const page_server_node &) = delete;
	page_server_node (page_server_node &&) = delete;

	page_server_node &operator= (const page_server_node &) = delete;
	page_server_node &operator= (page_server_node &&) = delete;

	void set_saved_lsa (log_lsa lsa);
	log_lsa get_saved_lsa () const;

	cubcomm::node get_conn_node () const;

      private:
	const cubcomm::node m_conn_node;
	std::atomic<log_lsa> m_saved_lsa;
    };

    class connection_handler
    {
      public:
	using page_server_conn_t = cubcomm::request_sync_client_server<tran_to_page_request, page_to_tran_request, std::string>;
	using request_handlers_map_t = std::map<page_to_tran_request, page_server_conn_t::incoming_request_handler_t>;

	connection_handler () = delete;

	connection_handler (const connection_handler &) = delete;
	connection_handler (connection_handler &&) = delete;

	connection_handler &operator= (const connection_handler &) = delete;
	connection_handler &operator= (connection_handler &&) = delete;

	void push_request (tran_to_page_request reqid, std::string &&payload);
	int send_receive (tran_to_page_request reqid, std::string &&payload_in, std::string &payload_out) const;

	virtual void disconnect ();
	const std::string get_channel_id () const;

      protected:
	connection_handler (cubcomm::channel &&chn, page_server_node &node, tran_server &ts,
			    request_handlers_map_t &&request_handlers);

	virtual request_handlers_map_t get_request_handlers ();

      protected:
	page_server_node &m_node; // in tran_server::m_node_vec;
	tran_server &m_ts;

      private:
	// Request handlers for requests in common

      private:
	std::unique_ptr<page_server_conn_t> m_conn;
    };

  protected:
    virtual connection_handler *create_connection_handler (cubcomm::channel &&chn, page_server_node &node,
	tran_server &ts) const = 0;

    // Booting functions that require specialization
    virtual bool get_remote_storage_config () = 0;

  protected:
    std::vector<std::unique_ptr<connection_handler>> m_page_server_conn_vec;
    std::vector<std::unique_ptr<page_server_node>> m_node_vec;

  private:
    int init_page_server_hosts (const char *db_name);
    int get_boot_info_from_page_server ();
    int connect_to_page_server (page_server_node &node, const char *db_name);

    /* send request to a specific connection */
    void push_request (size_t idx, tran_to_page_request reqid, std::string &&payload);
    int send_receive (size_t idx, tran_to_page_request reqid, std::string &&payload_in, std::string &payload_out) const;

    int parse_server_host (const std::string &host);
    int parse_page_server_hosts_config (std::string &hosts);

  private:
    cubcomm::server_server m_conn_type;
};

#endif // !_tran_server_HPP_
