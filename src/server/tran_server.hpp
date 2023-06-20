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
#include "log_lsa.hpp"
#include "request_sync_client_server.hpp"
#include "tran_page_requests.hpp"
#include "async_disconnect_handler.hpp"

#include <string>
#include <vector>
#include <shared_mutex>
#include <future>

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
      : m_main_conn { nullptr }
      , m_conn_type { conn_type }
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

    // Before disconnecting page server, make sure no message is being sent anymore to the page server.
    virtual void stop_outgoing_page_server_messages () = 0;

  protected:
    class connection_handler
    {
      public:
	using page_server_conn_t = cubcomm::request_sync_client_server<tran_to_page_request, page_to_tran_request, std::string>;
	using request_handlers_map_t = std::map<page_to_tran_request, page_server_conn_t::incoming_request_handler_t>;

      public:
	connection_handler () = delete;

	connection_handler (const connection_handler &) = delete;
	connection_handler (connection_handler &&) = delete;

	connection_handler &operator= (const connection_handler &) = delete;
	connection_handler &operator= (connection_handler &&) = delete;

	virtual ~connection_handler ();

	void set_connection (cubcomm::channel &&chn);
	void disconnect_async (bool with_disc_msg);
	void wait_async_disconnection ();

	int push_request (tran_to_page_request reqid, std::string &&payload);
	int send_receive (tran_to_page_request reqid, std::string &&payload_in, std::string &payload_out);

	const std::string get_channel_id () const;
	bool is_connected () const;

	virtual log_lsa get_saved_lsa () const = 0; // used in active_tran_server

      protected:
	connection_handler (tran_server &ts)
	  : m_ts { ts }
	  , m_state { state::OPEN }
	{ }

	virtual request_handlers_map_t get_request_handlers ();

	void push_request_internal (tran_to_page_request reqid, std::string &&payload);
	int send_receive_internal (tran_to_page_request reqid, std::string &&payload_in, std::string &payload_out);

      protected:
	tran_server &m_ts;

      private:
	/*
	 * The internal state of connection_handler. A connection_handler must be one of those states.
	 * And it's transitioned sequentially: OPEN -> CONNECTING -> CONNECTED -> DISCONNECTED -> OPEN
	 *
	 * The allow operations for each state are:
	 * +---------------+--------------+-------------+--------------+------------+------------+
	 * |     state     | accept a new | request     | request      | m_conn     | main       |
	 * |               | connection   | from inside | from outside | != nullptr | connection |
	 * +---------------+--------------+-------------+--------------+------------+------------+
	 * | OPEN          | O            | X           | X            | X          | X          |
	 * | CONNECTING    | X            | O           | X            | △          | X          |
	 * | CONNECTED     | X            | O           | O            | O          | O          |
	 * | DISCONNECTING | X            | O           | X            | △          | X          |
	 * +---------------+--------------+-------------+--------------+------------+------------+
	 *
	 * m_state and m_conn is coupled and mutexes for them are locked carefully to provide above operations.
	 */
	enum class state
	{
	  OPEN,
	  CONNECTING,
	  CONNECTED,
	  DISCONNECTING
	};

      private:
	// Request handlers for requests in common
	void receive_disconnect_request (page_server_conn_t::sequenced_payload &&a_sp);

	void send_disconnect_request ();

      private:
	std::unique_ptr<page_server_conn_t> m_conn;
	std::shared_mutex m_conn_mtx;

	state m_state;
	std::shared_mutex m_state_mtx;

	std::future<void> m_disconn_future; // To delete m_conn asynchronously and make sure there is only one m_conn at a time.
    };

  protected:
    virtual connection_handler *create_connection_handler (tran_server &ts) const = 0;

    // Booting functions that require specialization
    virtual bool get_remote_storage_config () = 0;

  protected:
    /*
     * Static information about available page server connection peers.
     * For now, this information is static. In the future this can be maintained dinamically (eg: via cluster
     * management sofware).
     */
    std::vector<cubcomm::node> m_connection_list;
    std::vector<std::unique_ptr<connection_handler>> m_page_server_conn_vec;

    connection_handler *m_main_conn;
    std::shared_mutex m_main_conn_mtx;

  private:
    int init_page_server_hosts (const char *db_name);
    int get_boot_info_from_page_server ();
    int connect_to_page_server (connection_handler &conn_handler, const cubcomm::node &node, const char *db_name);
    int reset_main_connection ();

    int parse_server_host (const std::string &host);
    int parse_page_server_hosts_config (std::string &hosts);

  private:
    cubcomm::server_server m_conn_type;
};

#endif // !_tran_server_HPP_
