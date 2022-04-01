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

#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include "communication_channel.hpp"
#include "request_client_server.hpp"
#include "request_sync_send_queue.hpp"
#include "request_sync_client_server.hpp"
#include "response_broker.hpp"

#include "comm_channel_mock.hpp"

#include <array>
#include <atomic>
#include <functional>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// common declarations
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static std::atomic<size_t> global_sent_request_count;
static std::atomic<size_t> global_handled_request_count;

static void init_globals ();                              // init global values for new test case

static void require_all_sent_requests_are_handled ();     // require that all sent requests have been handled

// reqcl sends the request with id = rid and payload = rid
// ReqCl can be either cubcomm::request_client<ReqId> or cubcomm::request_client_server<ReqId, ?>
template <typename ReqCl, typename ReqId>
static void send_request_id_as_message (ReqCl &reqcl, ReqId rid);

template <typename RSSQ, typename ReqId>
static void push_request_id_as_message (RSSQ &rssq, ReqId rid);

// Server request handler function for unpacking a ReqId and requiring to find ExpectedVal
template <typename ReqId, ReqId ExpectedVal>
static void
mock_check_expected_id (cubpacking::unpacker &upk);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stuff for one client and one server test case
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Request ids for requests sent by a client to a server
enum class reqids
{
  _0,
  _1
};
using test_request_client = cubcomm::request_client<reqids>;
using test_request_server = cubcomm::request_server<reqids>;
using test_handler_register_function = std::function<void (test_request_server &)>;

// Set a mock socket between a client and a server
static void mock_socket_between_client_and_server (const test_request_client &cl, const test_request_server &sr,
    mock_socket_direction &sockdir);
static void handler_register_mock_check_expected_id (test_request_server &req_sr);

// Environment for testing a client and server
class test_client_and_server_env
{
    static constexpr int MAX_TIMEOUT_IN_MS = 10;

  public:
    // ctor/dtor:
    // construct test server with custom handler functions
    test_client_and_server_env (test_handler_register_function &handler_register);
    ~test_client_and_server_env ();

    // get references to client/server
    test_request_client &get_client ();
    test_request_server &get_server ();

    // wait for all sent messages to be processed by server
    void wait_for_all_messages ();

  private:
    test_request_client m_client;	// the client
    test_request_server m_server;	// the server
    mock_socket_direction m_sockdir;	// socket direction
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stuff for two client-server test case
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Request ids exchanged by two client-server's
enum class reqids_1_to_2
{
  _0,
  _1,

  RESPOND  // for request_sync_client_server only
};

enum class reqids_2_to_1
{
  _0,
  _1,
  _2,

  RESPOND  // for request_sync_client_server only
};
// There are two type of request_client_server.
//
//  - One that sends the request ID's reqids_1_to_2 to the second client server and receives reqids_2_to_1
//  - Two that sends does the reverse: sends reqids_2_to_1 and receives reqids_1_to_2
using test_request_client_server_type_one = cubcomm::request_client_server<reqids_1_to_2, reqids_2_to_1>;
using test_request_client_server_type_two = cubcomm::request_client_server<reqids_2_to_1, reqids_1_to_2>;

static void register_request_handlers_request_client_server_one (test_request_client_server_type_one &req_cl_sr);
static void register_request_handlers_request_client_server_two (test_request_client_server_type_two &req_cl_sr);
static void mock_socket_between_two_client_servers (const test_request_client_server_type_one &clsr1,
    const test_request_client_server_type_two &clsr2,
    mock_socket_direction &sockdir_1_to_2,
    mock_socket_direction &sockdir_2_to_1);
// Testing environment for two client-server's
class test_two_client_server_env
{
  public:
    // ctor/dtor:
    test_two_client_server_env ();
    ~test_two_client_server_env ();

    // Get reference to a client-server instance
    test_request_client_server_type_one &get_cs_one ();
    test_request_client_server_type_two &get_cs_two ();

    // Wait for all sent messages, from both ends, to be processed by both servers
    void wait_for_all_messages ();

  private:
    test_request_client_server_type_one m_first_cs;	// the first client-server
    test_request_client_server_type_two m_second_cs;	// the second client-server
    mock_socket_direction m_sockdir_one_two;		// socket direction from first client to second server
    mock_socket_direction m_sockdir_two_one;		// socket direction from second client to first server
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stuff for request_queue_autosend test case.
// Also tests the request order. The request payload is extended to include the operation count.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Global op count, per each request id, is used by the server to check the requests are processed in the expected
// order.
std::array<int, 2> global_in_order_processed_op_count;
std::array<int, 2> global_out_of_order_processed_op_count;

struct payload_with_op_count : public cubpacking::packable_object
{
  int val;	  // static_cast<int> (requid)
  int op_count;	  // the op count

  payload_with_op_count () = default;
  payload_with_op_count (int a_val, int a_op_count)
    : val { a_val }, op_count { a_op_count }
  {
  }
  payload_with_op_count (const payload_with_op_count &) = delete;
  payload_with_op_count (payload_with_op_count &&) = default;

  payload_with_op_count &operator = (const payload_with_op_count &) = delete;
  payload_with_op_count &operator = (payload_with_op_count &&) = default;

  /* used at packing to get info on how much memory to reserve */
  size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const final;
  void pack (cubpacking::packer &serializator) const final;
  void unpack (cubpacking::unpacker &deserializator) final;
};

// Send both reqids and op_count into the request payload
template <typename RSSQ, typename ReqId>
static void push_rssq_message_id_and_op (RSSQ &rssq, ReqId reqid, int op_count);
template <typename SCS, typename ReqId>
static void push_scs_message_id_and_op (SCS &rssq, ReqId reqid, int op_count);
// Server handler checks both request id and request order
template <typename ReqId, ReqId ExpectedVal>
static void mock_check_expected_in_order_id_and_op_count (cubpacking::unpacker &upk);
// Function for registering mock_check_expected_in_order_id_and_op_count handlers
static void handler_register_mock_check_expected_in_order_id_and_op_count (test_request_server &req_sr);
// Server handler checks out-of-order request id processing
template <typename ReqId, ReqId ExpectedVal>
static void mock_check_expected_out_of_order_id_and_op_count (cubpacking::unpacker &upk);
// Function for registering mock_check_expected_out_of_order_id_and_op_count handlers
static void handler_register_mock_check_expected_out_of_order_id_and_op_count (test_request_server &req_sr);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stuff for two request_sync_client_server test case
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

using test_request_sync_client_server_one_t =
	cubcomm::request_sync_client_server<reqids_1_to_2, reqids_2_to_1, payload_with_op_count>;
using test_request_sync_client_server_two_t =
	cubcomm::request_sync_client_server<reqids_2_to_1, reqids_1_to_2, payload_with_op_count>;

using uq_test_request_sync_client_server_one_t = std::unique_ptr<test_request_sync_client_server_one_t>;
using uq_test_request_sync_client_server_two_t = std::unique_ptr<test_request_sync_client_server_two_t>;

// The request_sync_client_server type handler that requires to find ExpectedVal
template <typename ReqId, ReqId ExpectedVal, typename Payload>
static void mock_check_expected_id_sync (Payload &a_sp);

class test_two_request_sync_client_server_env
{
  public:
    test_two_request_sync_client_server_env ();
    ~test_two_request_sync_client_server_env ();

    test_request_sync_client_server_one_t &get_scs_one ();
    test_request_sync_client_server_two_t &get_scs_two ();

    void push_request_on_scs_one (reqids_1_to_2 msgid, int i);
    void push_request_on_scs_two (reqids_2_to_1 msgid, int i);

    void send_recv_request_on_scs_one (reqids_1_to_2 msgid, int i);
    void send_recv_request_on_scs_two (reqids_2_to_1 msgid, int i);

    void wait_for_all_messages ();

  private:
    template<typename T_SCS, typename T_MSGID>
    void push_request_and_increment_msg_count (T_SCS &scs, T_MSGID msgid, int i, std::atomic<size_t> &msg_count_inout);
    template<typename T_SCS, typename T_MSGID>
    void send_recv_and_increment_msg_count (T_SCS &scs, T_MSGID msgid, int i, std::atomic<size_t> &msg_count_inout);
    template<typename T_MSGID, T_MSGID T_VAL, typename T_SCS, typename T_SEQUENCED_PAYLOAD>
    void handle_req_and_respond (T_SCS &scs, T_SEQUENCED_PAYLOAD &payload, std::atomic<size_t> &msg_count_inout);

    template<reqids_2_to_1 T_VAL>
    void handle_req_and_respond_on_scs_one (test_request_sync_client_server_one_t::sequenced_payload &sp);
    template<reqids_1_to_2 T_VAL>
    void handle_req_and_respond_on_scs_two (test_request_sync_client_server_two_t::sequenced_payload &sp);

    uq_test_request_sync_client_server_one_t create_request_sync_client_server_one ();
    uq_test_request_sync_client_server_two_t create_request_sync_client_server_two ();

    void mock_socket_between_two_sync_client_servers ();

  private:
    uq_test_request_sync_client_server_one_t m_scs_one;
    uq_test_request_sync_client_server_two_t m_scs_two;
    mock_socket_direction m_sockdir_1_to_2;
    mock_socket_direction m_sockdir_2_to_1;

    std::atomic<size_t> m_total_1_to_2_message_count = 0;
    std::atomic<size_t> m_total_2_to_1_message_count = 0;

    std::atomic<size_t> m_total_response_requested = 0;
    std::atomic<size_t> m_total_response_sent = 0;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test definitions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE ("A client and a server", "")
{
  // Test how the requests sent by the request_client are handled by the request_server. The main thread of test case
  // act as the client and it sends the requests. The internal request_server thread handles the requests.
  //
  // Verify that:
  //    - the requests get handled on the server by the expected type of handler.
  //    - the number of requests sent is same as the number of requests handled.
  test_handler_register_function hreg_fun = handler_register_mock_check_expected_id;
  test_client_and_server_env env (hreg_fun);

  env.get_server ().start_thread ();

  send_request_id_as_message (env.get_client (), reqids::_0);
  send_request_id_as_message (env.get_client (), reqids::_1);
  send_request_id_as_message (env.get_client (), reqids::_1);
  send_request_id_as_message (env.get_client (), reqids::_1);
  send_request_id_as_message (env.get_client (), reqids::_0);
  send_request_id_as_message (env.get_client (), reqids::_1);
  send_request_id_as_message (env.get_client (), reqids::_0);

  env.wait_for_all_messages ();
  env.get_server ().stop_thread ();

  require_all_sent_requests_are_handled ();
}

TEST_CASE ("Two client-server communicate with each-other", "")
{
  // Test how two request_client_server instances interact with each-other. The main thread of the test case acts as
  // the client for both request_client_server and sends them requests. The requests are handled by the internal
  // threads of each request_client_server.
  //
  // Verify that:
  //    - all the requests, to both request_client_server instances, are handled by the expected type of handler
  //    - the number of requests sent is same as the number of requests handled.
  //

  test_two_client_server_env env;

  env.get_cs_one ().start_thread ();
  env.get_cs_two ().start_thread ();

  // Mix sends on both ends
  send_request_id_as_message (env.get_cs_one (), reqids_1_to_2::_0);
  send_request_id_as_message (env.get_cs_two (), reqids_2_to_1::_0);
  send_request_id_as_message (env.get_cs_one (), reqids_1_to_2::_1);
  send_request_id_as_message (env.get_cs_one (), reqids_1_to_2::_1);
  send_request_id_as_message (env.get_cs_two (), reqids_2_to_1::_0);
  send_request_id_as_message (env.get_cs_one (), reqids_1_to_2::_0);
  send_request_id_as_message (env.get_cs_one (), reqids_1_to_2::_0);
  send_request_id_as_message (env.get_cs_one (), reqids_1_to_2::_1);
  send_request_id_as_message (env.get_cs_one (), reqids_1_to_2::_1);
  send_request_id_as_message (env.get_cs_one (), reqids_1_to_2::_1);
  send_request_id_as_message (env.get_cs_two (), reqids_2_to_1::_1);
  send_request_id_as_message (env.get_cs_one (), reqids_1_to_2::_1);
  send_request_id_as_message (env.get_cs_two (), reqids_2_to_1::_0);
  send_request_id_as_message (env.get_cs_two (), reqids_2_to_1::_1);
  send_request_id_as_message (env.get_cs_two (), reqids_2_to_1::_0);
  send_request_id_as_message (env.get_cs_two (), reqids_2_to_1::_1);
  send_request_id_as_message (env.get_cs_two (), reqids_2_to_1::_0);

  env.wait_for_all_messages ();
  env.get_cs_one ().stop_thread ();
  env.get_cs_two ().stop_thread ();

  require_all_sent_requests_are_handled ();
}

TEST_CASE ("Verify request_sync_send_queue with request_client", "")
{
  // Test how requests are handled by request_sync_send_queue. Multiple threads may push requests, and multiple
  // threads may send the queued requests.
  //
  // Verify that:
  //	- all requests are handled by the expected type of handler
  //	- the number of requests sent is same as the number of requests handled

  test_handler_register_function hreg_fun = handler_register_mock_check_expected_id;
  test_client_and_server_env env (hreg_fun);

  using test_rssq = cubcomm::request_sync_send_queue<test_request_client, int>;

  test_rssq rssq (env.get_client (), nullptr);
  test_rssq::queue_type backbuffer;

  env.get_server ().start_thread ();

  SECTION ("Push and send all request on current thread")
  {
    push_request_id_as_message (rssq, reqids::_0);
    rssq.send_all (backbuffer);
    push_request_id_as_message (rssq, reqids::_1);
    push_request_id_as_message (rssq, reqids::_0);
    push_request_id_as_message (rssq, reqids::_0);
    rssq.send_all (backbuffer);
    push_request_id_as_message (rssq, reqids::_1);
    push_request_id_as_message (rssq, reqids::_0);
    push_request_id_as_message (rssq, reqids::_0);
    push_request_id_as_message (rssq, reqids::_1);
    push_request_id_as_message (rssq, reqids::_0);
    push_request_id_as_message (rssq, reqids::_0);
    push_request_id_as_message (rssq, reqids::_1);
    push_request_id_as_message (rssq, reqids::_0);
    push_request_id_as_message (rssq, reqids::_0);
    push_request_id_as_message (rssq, reqids::_1);
    push_request_id_as_message (rssq, reqids::_0);
    push_request_id_as_message (rssq, reqids::_0);
    rssq.send_all (backbuffer);
  }
  SECTION ("Push requests on other threads and send everything at the end on current thread")
  {
    std::thread t1 ([&rssq]
    {
      push_request_id_as_message (rssq, reqids::_0);
      push_request_id_as_message (rssq, reqids::_1);
      push_request_id_as_message (rssq, reqids::_0);
      push_request_id_as_message (rssq, reqids::_1);
      push_request_id_as_message (rssq, reqids::_0);
      push_request_id_as_message (rssq, reqids::_0);
      push_request_id_as_message (rssq, reqids::_1);
      push_request_id_as_message (rssq, reqids::_0);
      push_request_id_as_message (rssq, reqids::_1);
      push_request_id_as_message (rssq, reqids::_0);
    });
    std::thread t2 ([&rssq]
    {
      push_request_id_as_message (rssq, reqids::_0);
      push_request_id_as_message (rssq, reqids::_1);
      push_request_id_as_message (rssq, reqids::_1);
      push_request_id_as_message (rssq, reqids::_0);
      push_request_id_as_message (rssq, reqids::_1);
      push_request_id_as_message (rssq, reqids::_0);
      push_request_id_as_message (rssq, reqids::_1);
      push_request_id_as_message (rssq, reqids::_1);
      push_request_id_as_message (rssq, reqids::_1);
      push_request_id_as_message (rssq, reqids::_0);
    });
    t1.join ();
    t2.join ();
    rssq.send_all (backbuffer);
  }
  SECTION ("Push requests on other threads and send requests on other threads")
  {
    std::thread t1 ([&rssq]
    {
      for (size_t op = 0; op < 1000; ++op)
	{
	  push_request_id_as_message (rssq, reqids::_0);
	}
    });
    std::thread t2 ([&rssq]
    {
      for (size_t op = 0; op < 1000; ++op)
	{
	  push_request_id_as_message (rssq, reqids::_1);
	}
    });
    std::thread t3 ([&rssq]
    {
      test_rssq::queue_type local_buffer;
      auto start_time = std::chrono::system_clock::now ();
      while (std::chrono::system_clock::now () - start_time < std::chrono::seconds (1))
	{
	  rssq.send_all (local_buffer);
	  std::this_thread::sleep_for (std::chrono::milliseconds (1));
	}
    });
    std::thread t4 ([&rssq]
    {
      test_rssq::queue_type local_buffer;
      auto start_time = std::chrono::system_clock::now ();
      while (std::chrono::system_clock::now () - start_time < std::chrono::seconds (1))
	{
	  rssq.wait_not_empty_and_send_all (local_buffer, std::chrono::milliseconds (1));
	}
    });
    t1.join ();
    t2.join ();
    t3.join ();
    t4.join ();

    rssq.send_all (backbuffer);
  }

  env.wait_for_all_messages ();
  env.get_server ().stop_thread ();

  require_all_sent_requests_are_handled ();
}

TEST_CASE ("Test in-order request_queue_autosend", "")
{
  // Test the way requests are handled using a request_queue_autosend. All pushed requests are automatically send by
  // the request_queue_autosend. The requests are sent in the same order that they are pushed.
  //
  // Verify that:
  //	- all requests are handled by the expected type of handled
  //	- the requests of each thread are handled in the same order as they are pushed
  //	- the number of requests sent is the same as the number of requests handled
  //

  test_handler_register_function hreg_fun = handler_register_mock_check_expected_in_order_id_and_op_count;
  test_client_and_server_env env (hreg_fun);

  using test_rssq = cubcomm::request_sync_send_queue<test_request_client, payload_with_op_count>;

  test_rssq rssq (env.get_client (), nullptr);
  test_rssq::queue_type backbuffer;

  env.get_server ().start_thread ();

  std::thread t1 ([&rssq]
  {
    for (int op_count = 0; op_count < 1000; ++op_count)
      {
	push_rssq_message_id_and_op (rssq, reqids::_0, op_count);
      }
  });
  std::thread t2 ([&rssq]
  {
    for (int op_count = 0; op_count < 1000; ++op_count)
      {
	push_rssq_message_id_and_op (rssq, reqids::_1, op_count);
      }
  });

  cubcomm::request_queue_autosend<test_rssq> autosend (rssq);
  autosend.start_thread ();

  t1.join ();
  t2.join ();

  env.wait_for_all_messages ();
  env.get_server ().stop_thread ();
  autosend.stop_thread ();

  require_all_sent_requests_are_handled ();
}

TEST_CASE ("Test out-of-order request_queue_autosend", "")
{
  // Test the way requests are handled using a request_queue_autosend intermingled with forced "send all".
  // All pushed requests are automatically send by either the request_queue_autosend or by explicit sends invoked
  // from separate threads. The requests are sent deterministically but out of order.
  //
  // Verify that:
  //	- all requests are handled by the expected type of handled
  //	- the number of requests sent is the same as the number of requests handled
  //
  // This scenario does not occur in production but the test stresses the implementation to ensure robustness.

  test_handler_register_function hreg_fun = handler_register_mock_check_expected_out_of_order_id_and_op_count;
  test_client_and_server_env env (hreg_fun);

  using test_rssq = cubcomm::request_sync_send_queue<test_request_client, payload_with_op_count>;

  test_rssq rssq (env.get_client (), nullptr);
  test_rssq::queue_type backbuffer;

  env.get_server ().start_thread ();

  constexpr int total_op_count = 1000;

  std::thread t1 ([&rssq]
  {
    for (int op_count = 0; op_count < total_op_count; ++op_count)
      {
	push_rssq_message_id_and_op (rssq, reqids::_0, op_count);
      }
  });
  std::thread t2 ([&rssq]
  {
    for (int op_count = 0; op_count < total_op_count; ++op_count)
      {
	push_rssq_message_id_and_op (rssq, reqids::_1, op_count);
      }
  });
  std::thread t3 ([&rssq]
  {
    test_rssq::queue_type local_buffer;
    auto start_time = std::chrono::system_clock::now ();
    while (std::chrono::system_clock::now () - start_time < std::chrono::seconds (1))
      {
	rssq.send_all (local_buffer);
	std::this_thread::sleep_for (std::chrono::milliseconds (1));
      }
  });
  std::thread t4 ([&rssq]
  {
    test_rssq::queue_type local_buffer;
    auto start_time = std::chrono::system_clock::now ();
    while (std::chrono::system_clock::now () - start_time < std::chrono::seconds (1))
      {
	rssq.wait_not_empty_and_send_all (local_buffer, std::chrono::milliseconds (1));
      }
  });

  cubcomm::request_queue_autosend<test_rssq> autosend (rssq);
  autosend.start_thread ();

  t1.join ();
  t2.join ();
  t3.join ();
  t4.join ();

  env.wait_for_all_messages ();
  env.get_server ().stop_thread ();
  autosend.stop_thread ();

  require_all_sent_requests_are_handled ();

  REQUIRE (((total_op_count - 1) * total_op_count / 2) == global_out_of_order_processed_op_count[/*reqids::_*/0]);
  REQUIRE (((total_op_count - 1) * total_op_count / 2) == global_out_of_order_processed_op_count[/*reqids::_*/1]);
}

TEST_CASE ("Two request_sync_client_server communicate with each other", "[dbg]")
{
  test_two_request_sync_client_server_env env;

  constexpr int MESSAGE_COUNT = 4200;

#define BIND_ENV_FUNC(foo) \
  std::bind (&(test_two_request_sync_client_server_env::foo), std::ref (env), std::placeholders::_1, std::placeholders::_2)

#define REPEAT_REQUEST_WITH_MSGID(req_foo, msgid) \
  for (int i = 0; i < MESSAGE_COUNT; ++i) req_foo (msgid, i)

  std::vector<std::thread> threads;
  threads.emplace_back ([&] ()
  {
    REPEAT_REQUEST_WITH_MSGID (env.send_recv_request_on_scs_one, reqids_1_to_2::_0);
  });
  threads.emplace_back ([&] ()
  {
    REPEAT_REQUEST_WITH_MSGID (env.push_request_on_scs_one, reqids_1_to_2::_1);
  });
  threads.emplace_back ([&] ()
  {
    REPEAT_REQUEST_WITH_MSGID (env.send_recv_request_on_scs_two, reqids_2_to_1::_0);
  });
  threads.emplace_back ([&] ()
  {
    REPEAT_REQUEST_WITH_MSGID (env.push_request_on_scs_two, reqids_2_to_1::_1);
  });
  threads.emplace_back ([&] ()
  {
    REPEAT_REQUEST_WITH_MSGID (env.push_request_on_scs_two, reqids_2_to_1::_1);
  });

  for (auto &th : threads)
    {
      th.join ();
    }

  env.wait_for_all_messages ();

  require_all_sent_requests_are_handled ();
}

TEST_CASE ("Test response sequence number generator", "")
{
  // Test concurrent number generation, that all the generated numbers are unique

  cubcomm::response_sequence_number_generator rsn_gen;

  // Start threads that request numbers from the generator. Save all the numbers.
  //
  // In the end, compare all the generated numbers; they should be unique
  //
  constexpr size_t THREAD_COUNT = 10;
  constexpr size_t NUMBER_COUNT = 1000;
  using numbers_t = std::vector<cubcomm::response_sequence_number>;
  std::array<std::pair<std::thread, numbers_t>, THREAD_COUNT> threads_info;

  for (auto &ti : threads_info)
    {
      numbers_t &numbers = ti.second;
      numbers.reserve (NUMBER_COUNT);
      ti.first = std::thread ([&numbers, &rsn_gen] ()
      {
	numbers.push_back (rsn_gen.get_unique_number ());
      });
    }

  // Wait for everyone to finish
  for (auto &ti : threads_info)
    {
      ti.first.join ();
    }

  // Check all the generated numbers
  std::set<cubcomm::response_sequence_number> m_all_numbers;
  for (auto &ti : threads_info)
    {
      for (const auto n : ti.second)
	{
	  auto insert_ret = m_all_numbers.insert (n);

	  // Since all numbers are unique, the insertion must take place:
	  REQUIRE (insert_ret.second == true);
	}
    }
}

TEST_CASE ("Test response broker", "")
{
  // Test threads simulating requesters and a thread registering responses.

  constexpr size_t THREAD_COUNT = 10;
  constexpr size_t REQUEST_PER_THREAD_COUNT = 1000;
  constexpr size_t TOTAL_REQUEST_COUNT = THREAD_COUNT * REQUEST_PER_THREAD_COUNT;
  constexpr size_t BUCKET_COUNT = 30;

  cubcomm::response_sequence_number_generator rsn_gen;
  cubcomm::response_broker<cubcomm::response_sequence_number, css_error_code> broker (BUCKET_COUNT, NO_ERRORS,
      ERROR_ON_WRITE);

  std::vector<cubcomm::response_sequence_number> requested_rsn;
  std::mutex request_mutex;
  std::condition_variable request_condvar;

  std::thread responder_thread ([&] ()
  {
    size_t response_count = 0;
    while (response_count < TOTAL_REQUEST_COUNT)
      {
	std::unique_lock<std::mutex> ulock (request_mutex);
	request_condvar.wait (ulock, [&] ()
	{
	  return !requested_rsn.empty ();
	});

	size_t rsn_index = std::rand () % requested_rsn.size ();
	auto it = requested_rsn.begin () + rsn_index;
	cubcomm::response_sequence_number response_rsn = *it;
	requested_rsn.erase (it);

	ulock.unlock ();

	broker.register_response (response_rsn, std::move (response_rsn));
	++response_count;
      }
  }
			       );

  std::array<std::thread, THREAD_COUNT> requester_threads;
  for (auto &it : requester_threads)
    {
      it = std::thread ([&] ()
      {
	for (size_t i = 0; i < REQUEST_PER_THREAD_COUNT; ++i)
	  {
	    cubcomm::response_sequence_number rsn = rsn_gen.get_unique_number ();

	    {
	      std::lock_guard<std::mutex> lkguard (request_mutex);
	      requested_rsn.push_back (rsn);
	    }
	    request_condvar.notify_all ();

	    const auto [ response, error_code ] = broker.get_response (rsn);

	    REQUIRE (error_code == NO_ERRORS);
	    REQUIRE (response == rsn);
	  }
      }
		       );
    }

  // Wait for all threads to finish
  responder_thread.join ();
  for (auto &it : requester_threads)
    {
      it.join ();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// implementations
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void
init_globals ()
{
  global_sent_request_count = 0;
  global_handled_request_count = 0;

  global_in_order_processed_op_count[/*reqids::_*/0] = 0;
  global_in_order_processed_op_count[/*reqids::_*/1] = 0;

  global_out_of_order_processed_op_count[/*reqids::_*/0] = 0;
  global_out_of_order_processed_op_count[/*reqids::_*/1] = 0;
}

static void
require_all_sent_requests_are_handled ()
{
  REQUIRE (global_sent_request_count == global_handled_request_count);
}

template <typename T, T Val>
static void
mock_check_expected_id (cubpacking::unpacker &upk)
{
  T readval;
  upk.unpack_from_int (readval);
  REQUIRE (readval == Val);
  ++global_handled_request_count;
}

template <typename T, T Val, typename Payload>
static void
mock_check_expected_id_sync (Payload &a_sp)
{
  T readval = static_cast<T> (a_sp.pull_payload ().val);
  REQUIRE (readval == Val);
  ++global_handled_request_count;
}

template <typename ReqCl, typename ReqId>
static void
send_request_id_as_message (ReqCl &reqcl, ReqId rid)
{
  const css_error_code err_code = reqcl.send (rid, static_cast<int> (rid));
  REQUIRE (err_code == NO_ERRORS);
  ++global_sent_request_count;
}

template <typename RSSQ, typename ReqId>
static void
push_request_id_as_message (RSSQ &rssq, ReqId rid)
{
  rssq.push (rid, static_cast<int> (rid), nullptr);
  ++global_sent_request_count;
}

template <typename ReqId, ReqId ExpectedVal>
static void
mock_check_expected_in_order_id_and_op_count (cubpacking::unpacker &upk)
{
  payload_with_op_count payload;
  payload.unpack (upk);
  REQUIRE (payload.val == static_cast<int> (ExpectedVal));
  REQUIRE (global_in_order_processed_op_count[/*reqids::_*/payload.val] == payload.op_count);
  ++global_in_order_processed_op_count[/*reqids::_*/payload.val];

  ++global_handled_request_count;
}

template <typename ReqId, ReqId ExpectedVal>
static void
mock_check_expected_out_of_order_id_and_op_count (cubpacking::unpacker &upk)
{
  payload_with_op_count payload;
  payload.unpack (upk);
  REQUIRE (payload.val == static_cast<int> (ExpectedVal));
  global_out_of_order_processed_op_count[/*reqids::_*/payload.val] += payload.op_count;

  ++global_handled_request_count;
}

template <typename RSSQ, typename ReqId>
static void
push_rssq_message_id_and_op (RSSQ &rssq, ReqId reqid, int op_count)
{
  payload_with_op_count payload;
  payload.val = static_cast<int> (reqid);
  payload.op_count = op_count;

  rssq.push (reqid, std::move (payload), nullptr);

  ++global_sent_request_count;
}

template <typename SCS, typename ReqId>
static void
push_scs_message_id_and_op (SCS &scs, ReqId reqid, int op_count)
{
  payload_with_op_count payload;
  payload.val = static_cast<int> (reqid);
  payload.op_count = op_count;

  scs.push (reqid, std::move (payload));

  ++global_sent_request_count;
}

static void
handler_register_mock_check_expected_in_order_id_and_op_count (test_request_server &req_sr)
{
  cubcomm::request_server<reqids>::server_request_handler reqh0 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_in_order_id_and_op_count<reqids, reqids::_0> (upk);
  };
  cubcomm::request_server<reqids>::server_request_handler reqh1 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_in_order_id_and_op_count<reqids, reqids::_1> (upk);
  };
  req_sr.register_request_handler (reqids::_0, reqh0);
  req_sr.register_request_handler (reqids::_1, reqh1);
}

static void
handler_register_mock_check_expected_out_of_order_id_and_op_count (test_request_server &req_sr)
{
  cubcomm::request_server<reqids>::server_request_handler reqh0 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_out_of_order_id_and_op_count<reqids, reqids::_0> (upk);
  };
  cubcomm::request_server<reqids>::server_request_handler reqh1 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_out_of_order_id_and_op_count<reqids, reqids::_1> (upk);
  };
  req_sr.register_request_handler (reqids::_0, reqh0);
  req_sr.register_request_handler (reqids::_1, reqh1);
}

void
handler_register_mock_check_expected_id (test_request_server &req_sr)
{
  cubcomm::request_server<reqids>::server_request_handler reqh0 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_id<reqids, reqids::_0> (upk);
  };
  cubcomm::request_server<reqids>::server_request_handler reqh1 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_id<reqids, reqids::_1> (upk);
  };
  req_sr.register_request_handler (reqids::_0, reqh0);
  req_sr.register_request_handler (reqids::_1, reqh1);
}

void
mock_socket_between_client_and_server (const test_request_client &cl, const test_request_server &sr,
				       mock_socket_direction &sockdir)
{
  add_socket_direction (cl.get_channel ().get_channel_id (), sr.get_channel ().get_channel_id (), sockdir, true);
}

test_client_and_server_env::test_client_and_server_env (test_handler_register_function &handler_register)
  : m_client { cubcomm::channel { MAX_TIMEOUT_IN_MS, "client" } }
  , m_server { cubcomm::channel { "server" } }
  , m_sockdir ()
{
  handler_register (m_server);
  mock_socket_between_client_and_server (m_client, m_server, m_sockdir);
  init_globals ();
}

test_client_and_server_env::~test_client_and_server_env ()
{
  clear_socket_directions ();
}

test_request_client &
test_client_and_server_env::get_client ()
{
  return m_client;
}

test_request_server &
test_client_and_server_env::get_server ()
{
  return m_server;
}

void
test_client_and_server_env::wait_for_all_messages ()
{
  m_sockdir.wait_until_message_count (global_sent_request_count * 2);  // each request sends two messages
  m_sockdir.wait_for_all_messages ();
}

void
register_request_handlers_request_client_server_one (test_request_client_server_type_one &req_cl_sr)
{
  // handles reqids_2_to_1
  test_request_client_server_type_one::server_request_handler reqh0 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_id<reqids_2_to_1, reqids_2_to_1::_0> (upk);
  };
  test_request_client_server_type_one::server_request_handler reqh1 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_id<reqids_2_to_1, reqids_2_to_1::_1> (upk);
  };
  test_request_client_server_type_one::server_request_handler reqh2 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_id<reqids_2_to_1, reqids_2_to_1::_2> (upk);
  };
  req_cl_sr.register_request_handler (reqids_2_to_1::_0, reqh0);
  req_cl_sr.register_request_handler (reqids_2_to_1::_1, reqh1);
  req_cl_sr.register_request_handler (reqids_2_to_1::_2, reqh2);
}

void
register_request_handlers_request_client_server_two (test_request_client_server_type_two &req_cl_sr)
{
  // handles reqids_1_to_2
  test_request_client_server_type_two::server_request_handler reqh0 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_id<reqids_1_to_2, reqids_1_to_2::_0> (upk);
  };
  test_request_client_server_type_two::server_request_handler reqh1 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_id<reqids_1_to_2, reqids_1_to_2::_1> (upk);
  };
  req_cl_sr.register_request_handler (reqids_1_to_2::_0, reqh0);
  req_cl_sr.register_request_handler (reqids_1_to_2::_1, reqh1);
}

void
mock_socket_between_two_client_servers (const test_request_client_server_type_one &clsr1,
					const test_request_client_server_type_two &clsr2,
					mock_socket_direction &sockdir_1_to_2, mock_socket_direction &sockdir_2_to_1)
{
  add_socket_direction (clsr1.get_channel ().get_channel_id (), clsr2.get_channel ().get_channel_id (), sockdir_1_to_2,
			false);
  add_socket_direction (clsr2.get_channel ().get_channel_id (), clsr1.get_channel ().get_channel_id (), sockdir_2_to_1,
			true);
}


test_two_client_server_env::test_two_client_server_env ()
  : m_first_cs { ::cubcomm::channel { "client_server_one" } }
  , m_second_cs { ::cubcomm::channel { "client_server_two" } }
  , m_sockdir_one_two ()
  , m_sockdir_two_one ()
{
  register_request_handlers_request_client_server_one (m_first_cs);
  register_request_handlers_request_client_server_two (m_second_cs);
  mock_socket_between_two_client_servers (m_first_cs, m_second_cs, m_sockdir_one_two, m_sockdir_two_one);
  init_globals ();
}

test_two_client_server_env::~test_two_client_server_env ()
{
  clear_socket_directions ();
}

test_request_client_server_type_one &
test_two_client_server_env::get_cs_one ()
{
  return m_first_cs;
}

test_request_client_server_type_two &
test_two_client_server_env::get_cs_two ()
{
  return m_second_cs;
}

void
test_two_client_server_env::wait_for_all_messages ()
{
  m_sockdir_one_two.wait_for_all_messages ();
  m_sockdir_two_one.wait_for_all_messages ();
}

void
payload_with_op_count::pack (cubpacking::packer &serializator) const
{
  serializator.pack_all (val, op_count);
}

void
payload_with_op_count::unpack (cubpacking::unpacker &deserializator)
{
  deserializator.unpack_all (val, op_count);
}

size_t
payload_with_op_count::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset /* = 0 */) const
{
  size_t size = 0;

  size += serializator.get_packed_int_size (start_offset + size);
  size += serializator.get_packed_int_size (start_offset + size);

  return size;
}

test_two_request_sync_client_server_env::test_two_request_sync_client_server_env ()
  : m_scs_one { create_request_sync_client_server_one () }
  , m_scs_two { create_request_sync_client_server_two () }
{
  mock_socket_between_two_sync_client_servers ();
  init_globals ();
}

test_two_request_sync_client_server_env::~test_two_request_sync_client_server_env ()
{
  // reset pointers (aka: stop threads) before clearing mocked socket directions
  // otherwise, internal checks are invalidated after the socket directions are cleared
  m_scs_one.reset (nullptr);
  m_scs_two.reset (nullptr);

  clear_socket_directions ();
}

test_request_sync_client_server_one_t &
test_two_request_sync_client_server_env::get_scs_one ()
{
  return *m_scs_one.get ();
}

test_request_sync_client_server_two_t &
test_two_request_sync_client_server_env::get_scs_two ()
{
  return *m_scs_two.get ();
}

void test_two_request_sync_client_server_env::wait_for_all_messages ()
{
  m_sockdir_1_to_2.wait_until_message_count (m_total_1_to_2_message_count * 2);
  m_sockdir_2_to_1.wait_until_message_count (m_total_2_to_1_message_count * 2);

  m_sockdir_1_to_2.wait_for_all_messages ();
  m_sockdir_2_to_1.wait_for_all_messages ();
}

template<typename T_SCS, typename T_MSGID>
void
test_two_request_sync_client_server_env::push_request_and_increment_msg_count (
	T_SCS &scs, T_MSGID msgid, int i, std::atomic<size_t> &msg_count_inout)
{
  ++msg_count_inout;

  push_scs_message_id_and_op (scs, msgid, i);
}

template<typename T_SCS, typename T_MSGID>
void
test_two_request_sync_client_server_env::send_recv_and_increment_msg_count (
	T_SCS &scs, T_MSGID msgid, int i, std::atomic<size_t> &msg_count_inout)
{
  ++msg_count_inout;

  payload_with_op_count request_payload;
  request_payload.val = static_cast<int> (msgid);
  request_payload.op_count = i;

  payload_with_op_count response_payload;

  const css_error_code error_code = scs.send_recv (msgid, std::move (request_payload), response_payload);
  REQUIRE (error_code == NO_ERRORS);
  REQUIRE (response_payload.val == static_cast<int> (msgid));
  REQUIRE (response_payload.op_count == i + 1);	  // it is incremented by response handler

  ++global_sent_request_count;
}

template<typename T_MSGID, T_MSGID T_VAL, typename T_SCS, typename T_SEQUENCED_PAYLOAD>
void
test_two_request_sync_client_server_env::handle_req_and_respond (T_SCS &scs, T_SEQUENCED_PAYLOAD &sp,
    std::atomic<size_t> &msg_count_inout)
{
  payload_with_op_count payload = sp.pull_payload ();
  mock_check_expected_id_sync<T_MSGID, T_VAL, T_SEQUENCED_PAYLOAD> (sp);

  payload.op_count++;
  sp.push_payload (std::move (payload));

  ++msg_count_inout;
  scs.respond (std::move (sp));
}

void
test_two_request_sync_client_server_env::push_request_on_scs_one (reqids_1_to_2 msgid, int i)
{
  push_request_and_increment_msg_count (get_scs_one (), msgid, i, m_total_1_to_2_message_count);
}

void
test_two_request_sync_client_server_env::push_request_on_scs_two (reqids_2_to_1 msgid, int i)
{
  push_request_and_increment_msg_count (get_scs_two (), msgid, i, m_total_2_to_1_message_count);
}

void
test_two_request_sync_client_server_env::send_recv_request_on_scs_one (reqids_1_to_2 msgid, int i)
{
  send_recv_and_increment_msg_count (get_scs_one (), msgid, i, m_total_1_to_2_message_count);
}

void
test_two_request_sync_client_server_env::send_recv_request_on_scs_two (reqids_2_to_1 msgid, int i)
{
  send_recv_and_increment_msg_count (get_scs_two (), msgid, i, m_total_2_to_1_message_count);
}

template<reqids_2_to_1 T_VAL>
void
test_two_request_sync_client_server_env::handle_req_and_respond_on_scs_one (
	test_request_sync_client_server_one_t::sequenced_payload &sp)
{
  handle_req_and_respond<reqids_2_to_1, T_VAL> (get_scs_one (), sp, m_total_1_to_2_message_count);
}

template<reqids_1_to_2 T_VAL>
void
test_two_request_sync_client_server_env::handle_req_and_respond_on_scs_two (
	test_request_sync_client_server_two_t::sequenced_payload &sp)
{
  handle_req_and_respond<reqids_1_to_2, T_VAL> (get_scs_two (), sp, m_total_2_to_1_message_count);
}

uq_test_request_sync_client_server_one_t
test_two_request_sync_client_server_env::create_request_sync_client_server_one ()
{
  const int max_timeout_in_ms = 10;
  cubcomm::channel chn{ max_timeout_in_ms };
  chn.set_channel_name ("sync_client_server_one");

  // handle requests 2 to 1
  test_request_sync_client_server_one_t::incoming_request_handler_t req_handler_0 =
	  [this] (test_request_sync_client_server_one_t::sequenced_payload &a_sp)
  {
    handle_req_and_respond_on_scs_one<reqids_2_to_1::_0> (a_sp);
  };
  test_request_sync_client_server_one_t::incoming_request_handler_t req_handler_1 =
	  [] (test_request_sync_client_server_one_t::sequenced_payload &a_sp)
  {
    mock_check_expected_id_sync<reqids_2_to_1, reqids_2_to_1::_1> (a_sp);
  };
  test_request_sync_client_server_one_t::incoming_request_handler_t req_handler_2 =
	  [] (test_request_sync_client_server_one_t::sequenced_payload &a_sp)
  {
    mock_check_expected_id_sync<reqids_2_to_1, reqids_2_to_1::_2> (a_sp);
  };

  uq_test_request_sync_client_server_one_t scs_one
  {
    new test_request_sync_client_server_one_t (std::move (chn),
    {
      { reqids_2_to_1::_0, req_handler_0 },
      { reqids_2_to_1::_1, req_handler_1 },
      { reqids_2_to_1::_2, req_handler_2 }
    }, reqids_1_to_2::RESPOND, reqids_2_to_1::RESPOND, 10, nullptr)
  };
  scs_one->start ();

  return scs_one;
}

uq_test_request_sync_client_server_two_t
test_two_request_sync_client_server_env::create_request_sync_client_server_two ()
{
  const int max_timeout_in_ms = 10;
  cubcomm::channel chn{ max_timeout_in_ms };
  chn.set_channel_name ("sync_client_server_two");

  // handle requests 1 to 2
  test_request_sync_client_server_two_t::incoming_request_handler_t req_handler_0 =
	  [this] (test_request_sync_client_server_two_t::sequenced_payload &a_sp)
  {
    handle_req_and_respond_on_scs_two<reqids_1_to_2::_0> (a_sp);
  };
  test_request_sync_client_server_two_t::incoming_request_handler_t req_handler_1 =
	  [] (test_request_sync_client_server_two_t::sequenced_payload &a_sp)
  {
    mock_check_expected_id_sync<reqids_1_to_2, reqids_1_to_2::_1> (a_sp);
  };

  uq_test_request_sync_client_server_two_t scs_two
  {
    new test_request_sync_client_server_two_t (std::move (chn),
    {
      { reqids_1_to_2::_0, req_handler_0 },
      { reqids_1_to_2::_1, req_handler_1 }
    }, reqids_2_to_1::RESPOND, reqids_1_to_2::RESPOND, 10, nullptr)
  };
  scs_two->start ();

  return scs_two;
}

void
test_two_request_sync_client_server_env::mock_socket_between_two_sync_client_servers ()
{
  add_socket_direction (m_scs_one->get_underlying_channel_id (), m_scs_two->get_underlying_channel_id (),
			m_sockdir_1_to_2, false);
  add_socket_direction (m_scs_two->get_underlying_channel_id (), m_scs_one->get_underlying_channel_id (),
			m_sockdir_2_to_1, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mock CUBRID stuff
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "error_manager.h"
#include "system_parameter.h"
#include "object_representation.h"

void
_er_log_debug (const char *, const int, const char *, ...)
{
  // do nothing
}

void
er_set (int, const char *, const int, int, int, ...)
{
  // nop
}

bool
prm_get_bool_value (PARAM_ID prmid)
{
  return false;
}

int
or_packed_value_size (const DB_VALUE *, int, int, int )
{
  return 0;
}

char *
or_pack_value (char *, DB_VALUE *)
{
  return nullptr;
}

char *
or_unpack_value (const char *, DB_VALUE *)
{
  return nullptr;
}
