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
#include "async_disconnect_handler.hpp"

#include <string>
#include <vector>
#include <shared_mutex>

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
    int send_receive (tran_to_page_request reqid, std::string &&payload_in, std::string &payload_out);

    void disconnect_all_page_servers ();
    bool is_page_server_connected () const;
    virtual bool uses_remote_storage () const;

  protected:
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

	~connection_handler ();

	void push_request (tran_to_page_request reqid, std::string &&payload);
	int send_receive (tran_to_page_request reqid, std::string &&payload_in, std::string &payload_out) const;

	/* Do all needed job to disconnect in advance:
	 * e.g.) stop connection-specific msg generators, send SEND_DISCONNECT_MSG to the page server
	 *
	 * It's acutally done in the ~connection_handler() to release resources for the connection
	 * such as its socket, internal send/receive threads.
	 * And this is usually done in m_async_disconnect_handler.
	 *
	 */
	virtual void prepare_disconnection ();
	const std::string get_channel_id () const;

      protected:
	connection_handler (cubcomm::channel &&chn, tran_server &ts, request_handlers_map_t &&request_handlers);

	virtual request_handlers_map_t get_request_handlers ();

      protected:
	tran_server &m_ts;

      private:
	// Request handlers for requests in common
	void receive_disconnect_request (page_server_conn_t::sequenced_payload &a_ip);

      private:
	std::unique_ptr<page_server_conn_t> m_conn;
    };

  protected:
    virtual connection_handler *create_connection_handler (cubcomm::channel &&chn, tran_server &ts) const = 0;

    // Booting functions that require specialization
    virtual bool get_remote_storage_config () = 0;

    // Before disconnecting page server, make sure no message is being sent anymore to the page server.
    virtual void stop_outgoing_page_server_messages () = 0;

  private:
    int init_page_server_hosts (const char *db_name);
    int get_boot_info_from_page_server ();
    int connect_to_page_server (const cubcomm::node &node, const char *db_name);

    int parse_server_host (const std::string &host);
    int parse_page_server_hosts_config (std::string &hosts);

  private:
    std::vector<cubcomm::node> m_connection_list;

    /*
     * When disconnecting a connection by m_async_disconnect_handler,
     * a connection can be accessed to communicate in push_request and send_receive.
     * To prevent them from accessing a destroyed connection, the shared_ptr is used.
     * Depending on scheduling, a connection can be rarely destroyed after its communication in the functions above.
     */
    std::vector<std::shared_ptr<connection_handler>> m_page_server_conn_vec;
    std::shared_mutex m_page_server_conn_vec_mtx; // For excluiveness between communication and disconnection.
    async_disconnect_handler<std::shared_ptr<connection_handler>> m_async_disconnect_handler;

    cubcomm::server_server m_conn_type;
};

#endif // !_tran_server_HPP_
