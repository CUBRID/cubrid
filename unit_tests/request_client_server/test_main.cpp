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

#include "comm_channel_mock.hpp"

#include <array>
#include <atomic>
#include <functional>

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

//
// Stuff for one client and one server test case
//

// Request ids for requests sent by a client to a server
enum class reqids
{
  _0,
  _1
};
using test_request_client = cubcomm::request_client<reqids>;
using test_request_server = cubcomm::request_server<reqids>;
using test_handler_register_function = std::function<void (test_request_server &)>;

// Create a request_client for this test
static test_request_client create_request_client ();
// Create a request_server for this test. Different functions can be registered by passing different handler_register
static test_request_server create_request_server (const test_handler_register_function &handler_register);
// Set a mock socket between a client and a server
static void mock_socket_between_client_and_server (const test_request_client &cl, const test_request_server &sr,
    mock_socket_direction &sockdir);
static void handler_register_mock_check_expected_id (test_request_server &req_sr);

// Environment for testing a client and server
class test_client_and_server_env
{
  public:
    // ctor/dtor:
    // construct test server with custom handler functions
    test_client_and_server_env (test_handler_register_function &hreg);
    ~test_client_and_server_env ();

    // get references to client/server
    test_request_client &get_client ();
    test_request_server &get_server ();

    // move client
    test_request_client move_client ();

    // wait for all sent messages to be processed by server
    void wait_for_all_messages ();

  private:
    test_request_client m_client;	// the client
    test_request_server m_server;	// the server
    mock_socket_direction m_sockdir;	// socket direction
};

//
// Stuff for two client-server test case
//

// Request ids exchanged by two client-server's
enum class reqids_1_to_2
{
  _0,
  _1
};

enum class reqids_2_to_1
{
  _0,
  _1,
  _2
};
// There are two type of request_client_server.
//
//  - One that sends the request ID's reqids_1_to_2 to the second client server and receives reqids_2_to_1
//  - Two that sends does the reverse: sends reqids_2_to_1 and receives reqids_1_to_2
using test_request_client_server_type_one = cubcomm::request_client_server<reqids_1_to_2, reqids_2_to_1>;
using test_request_client_server_type_two = cubcomm::request_client_server<reqids_2_to_1, reqids_1_to_2>;

static test_request_client_server_type_one create_request_client_server_one ();
static test_request_client_server_type_two create_request_client_server_two ();
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

//
// Stuff for request_queue_autosend test case.
// Also tests the request order. The request payload is extended to include the operation count.
//

// Global op count, per each request id, is used by the server to check the requests are processed in the expected
// order.
std::array<int, 2> global_op_count;

struct payload_with_op_count : public cubpacking::packable_object
{
  int val;	  // static_cast<int> (requid)
  int op_count;	  // the op count

  /* used at packing to get info on how much memory to reserve */
  size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const final;
  void pack (cubpacking::packer &serializator) const final;
  void unpack (cubpacking::unpacker &deserializator) final;
};

// Send both reqids and op_count into the request payload
template <typename RSSQ, typename ReqId>
static void push_message_id_and_op (RSSQ &rssq, ReqId reqid, int op_count);
// Server handler checks both request id and request order
template <typename ReqId, ReqId ExpectedVal>
static void mock_check_expected_id_and_op_count (cubpacking::unpacker &upk);
// Function for registering mock_check_expected_id_and_op_count handlers
static void handler_register_mock_check_expected_id_and_op_count (test_request_server &req_sr);

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

  test_rssq rssq (env.move_client ());
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

TEST_CASE ("Test request_queue_autosend", "")
{
  // Test the way requests are handled using a request_queue_autosend. All pushed requests are automatically send by
  // the request_queue_autosend. The requests are sent in the same order that they are pushed.
  //
  // Verify that:
  //	- all requests are handled by the expected type of handled
  //	- the requests of each thread are handled in the same order as they are pushed
  //	- the number of requests sent is the same as the number of requests handled
  //

  test_handler_register_function hreg_fun = handler_register_mock_check_expected_id_and_op_count;
  test_client_and_server_env env (hreg_fun);

  using test_rssq = cubcomm::request_sync_send_queue<test_request_client, payload_with_op_count>;

  test_rssq rssq (env.move_client ());
  test_rssq::queue_type backbuffer;

  env.get_server ().start_thread ();

  std::thread t1 ([&rssq]
  {
    for (int op_count = 0; op_count < 1000; ++op_count)
      {
	push_message_id_and_op (rssq, reqids::_0, op_count);
      }
  });
  std::thread t2 ([&rssq]
  {
    for (int op_count = 0; op_count < 1000; ++op_count)
      {
	push_message_id_and_op (rssq, reqids::_1, op_count);
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

static void
init_globals ()
{
  global_sent_request_count = 0;
  global_handled_request_count = 0;

  global_op_count[0] = 0;
  global_op_count[1] = 0;
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

template <typename ReqCl, typename ReqId>
static void
send_request_id_as_message (ReqCl &reqcl, ReqId rid)
{
  reqcl.send (rid, static_cast<int> (rid));
  ++global_sent_request_count;
}

template <typename RSSQ, typename ReqId>
static void
push_request_id_as_message (RSSQ &rssq, ReqId rid)
{
  rssq.push (rid, static_cast<int> (rid));
  ++global_sent_request_count;
}

template <typename ReqId, ReqId ExpectedVal>
static void
mock_check_expected_id_and_op_count (cubpacking::unpacker &upk)
{
  payload_with_op_count payload;
  payload.unpack (upk);
  REQUIRE (payload.val == static_cast<int> (ExpectedVal));
  REQUIRE (global_op_count[payload.val] == payload.op_count);
  ++global_op_count[payload.val];

  ++global_handled_request_count;
}

template <typename RSSQ, typename ReqId>
static void
push_message_id_and_op (RSSQ &rssq, ReqId reqid, int op_count)
{
  payload_with_op_count payload;
  payload.val = static_cast<int> (reqid);
  payload.op_count = op_count;

  rssq.push (reqid, std::move (payload));

  ++global_sent_request_count;
}

static void
handler_register_mock_check_expected_id_and_op_count (test_request_server &req_sr)
{
  cubcomm::request_server<reqids>::server_request_handler reqh0 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_id_and_op_count<reqids, reqids::_0> (upk);
  };
  cubcomm::request_server<reqids>::server_request_handler reqh1 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_id_and_op_count<reqids, reqids::_1> (upk);
  };
  req_sr.register_request_handler (reqids::_0, reqh0);
  req_sr.register_request_handler (reqids::_1, reqh1);
}

test_request_client
create_request_client ()
{
  cubcomm::channel chncl;
  chncl.set_channel_name ("client");

  test_request_client req_cl (std::move (chncl));

  return std::move (req_cl);
}

test_request_server
create_request_server (const test_handler_register_function &handler_register)
{
  cubcomm::channel chnsr;
  chnsr.set_channel_name ("server");

  test_request_server req_sr (std::move (chnsr));

  handler_register (req_sr);

  return req_sr;
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
  add_socket_direction (cl.get_channel ().get_channel_id (), sr.get_channel ().get_channel_id (), sockdir);
}

test_request_client_server_type_one
create_request_client_server_one ()
{
  cubcomm::channel chn;
  chn.set_channel_name ("client_server_one");

  test_request_client_server_type_one req_clsr = (std::move (chn));

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
  req_clsr.register_request_handler (reqids_2_to_1::_0, reqh0);
  req_clsr.register_request_handler (reqids_2_to_1::_1, reqh1);
  req_clsr.register_request_handler (reqids_2_to_1::_2, reqh2);

  return req_clsr;
}

test_request_client_server_type_two
create_request_client_server_two ()
{
  cubcomm::channel chn;
  chn.set_channel_name ("client_server_two");

  test_request_client_server_type_two req_clsr = (std::move (chn));

  // handles reqids_2_to_1
  test_request_client_server_type_two::server_request_handler reqh0 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_id<reqids_1_to_2, reqids_1_to_2::_0> (upk);
  };
  test_request_client_server_type_two::server_request_handler reqh1 = [] (cubpacking::unpacker &upk)
  {
    mock_check_expected_id<reqids_1_to_2, reqids_1_to_2::_1> (upk);
  };
  req_clsr.register_request_handler (reqids_1_to_2::_0, reqh0);
  req_clsr.register_request_handler (reqids_1_to_2::_1, reqh1);

  return req_clsr;
}

void
mock_socket_between_two_client_servers (const test_request_client_server_type_one &clsr1,
					const test_request_client_server_type_two &clsr2,
					mock_socket_direction &sockdir_1_to_2, mock_socket_direction &sockdir_2_to_1)
{
  add_socket_direction (clsr1.get_channel ().get_channel_id (), clsr2.get_channel ().get_channel_id (), sockdir_1_to_2);
  add_socket_direction (clsr2.get_channel ().get_channel_id (), clsr1.get_channel ().get_channel_id (), sockdir_2_to_1);
}

test_client_and_server_env::test_client_and_server_env (test_handler_register_function &hreg)
  : m_client (create_request_client ())
  , m_server (create_request_server (hreg))
  , m_sockdir ()
{
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

test_request_client
test_client_and_server_env::move_client ()
{
  return std::move (m_client);
}

void
test_client_and_server_env::wait_for_all_messages ()
{
  m_sockdir.wait_until_message_count (global_sent_request_count * 2);  // each request sends two messages
  m_sockdir.wait_for_all_messages ();
}

test_two_client_server_env::test_two_client_server_env ()
  : m_first_cs (create_request_client_server_one())
  , m_second_cs (create_request_client_server_two())
  , m_sockdir_one_two ()
  , m_sockdir_two_one ()
{
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

//
// Mock CUBRID stuff
//
#include "error_manager.h"
#include "system_parameter.h"
#include "object_representation.h"

void
_er_log_debug (const char *file_name, const int line_no, const char *fmt, ...)
{
  // do nothing
}

bool
prm_get_bool_value (PARAM_ID prmid)
{
  return false;
}

int
or_packed_value_size (const DB_VALUE *value, int collapse_null, int include_domain, int include_domain_classoids)
{
  return 0;
}

char *
or_pack_value (char *buf, DB_VALUE *value)
{
  return nullptr;
}

char *
or_unpack_value (const char *buf, DB_VALUE *value)
{
  return nullptr;
}

namespace cubcomm
{
  void
  er_log_send_request (const channel &chn, int msgid, size_t size)
  {

  }

  void
  er_log_recv_request (const channel &chn, int msgid, size_t size)
  {

  }

  void
  er_log_send_fail (const channel &chn, css_error_code err)
  {

  }

  void
  er_log_recv_fail (const channel &chn, css_error_code err)
  {

  }
}
