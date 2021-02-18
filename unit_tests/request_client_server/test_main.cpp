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

#include "comm_channel_mock.hpp"

#include <functional>

static size_t global_sent_request_count;
static size_t global_handled_request_count;

static void init_globals ();                              // init global values for new test case

static void require_all_sent_requests_are_handled ();     // require that all sent requests have been handled

// reqcl sends the request with id = rid and payload = rid
// ReqCl can be either cubcomm::request_client<ReqId> or cubcomm::request_client_server<ReqId, ?>
template <typename ReqCl, typename ReqId>
static void send_request_id_as_message (ReqCl &reqcl, ReqId rid);

// Server request handler function for unpacking a ReqId and requiring to find ExpectedVal
template <typename ReqId, ReqId ExpectedVal>
static void
mock_check_expected_id (cubpacking::unpacker &upk);

//
// Stuff for one client and one server test case
//

enum class reqids
{
  _0,
  _1
};
using test_request_client = cubcomm::request_client<reqids>;
using test_request_server = cubcomm::request_server<reqids>;

static test_request_client create_request_client ();
static test_request_server create_request_server ();
static void mock_socket_between_client_and_server (const test_request_client &cl, const test_request_server &sr,
    mock_socket_direction &sockdir);

//
// Stuff for two client-server test case
//

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

TEST_CASE ("A client and a server", "")
{
  // Test how the requests sent by the request_client are handled by the request_server. The main thread of test case
  // act as the client and it sends the requests. The internal request_server thread handles the requests.
  //
  // Verify that:
  //    - the requests get handled on the server by the expected type of handler.
  //    - the number of requests sent is same as the number of requests handled.

  test_request_client req_client = create_request_client ();
  test_request_server req_server = create_request_server ();

  mock_socket_direction sockdir;
  mock_socket_between_client_and_server (req_client, req_server, sockdir);

  init_globals ();

  req_server.start_thread ();

  send_request_id_as_message (req_client, reqids::_0);
  send_request_id_as_message (req_client, reqids::_1);
  send_request_id_as_message (req_client, reqids::_1);
  send_request_id_as_message (req_client, reqids::_1);
  send_request_id_as_message (req_client, reqids::_0);
  send_request_id_as_message (req_client, reqids::_1);
  send_request_id_as_message (req_client, reqids::_0);

  sockdir.wait_for_all_messages ();

  req_server.stop_thread ();

  require_all_sent_requests_are_handled ();

  clear_socket_directions ();
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

  test_request_client_server_type_one req_client_server_1 = create_request_client_server_one ();
  test_request_client_server_type_two req_client_server_2 = create_request_client_server_two ();

  mock_socket_direction sockdir_1_to_2;
  mock_socket_direction sockdir_2_to_1;
  mock_socket_between_two_client_servers (req_client_server_1, req_client_server_2, sockdir_1_to_2, sockdir_2_to_1);

  init_globals ();

  req_client_server_1.start_thread ();
  req_client_server_2.start_thread ();

  // Mix sends on both ends
  send_request_id_as_message (req_client_server_1, reqids_1_to_2::_0);
  send_request_id_as_message (req_client_server_2, reqids_2_to_1::_0);
  send_request_id_as_message (req_client_server_1, reqids_1_to_2::_1);
  send_request_id_as_message (req_client_server_1, reqids_1_to_2::_1);
  send_request_id_as_message (req_client_server_2, reqids_2_to_1::_0);
  send_request_id_as_message (req_client_server_1, reqids_1_to_2::_0);
  send_request_id_as_message (req_client_server_1, reqids_1_to_2::_0);
  send_request_id_as_message (req_client_server_1, reqids_1_to_2::_1);
  send_request_id_as_message (req_client_server_1, reqids_1_to_2::_1);
  send_request_id_as_message (req_client_server_1, reqids_1_to_2::_1);
  send_request_id_as_message (req_client_server_2, reqids_2_to_1::_1);
  send_request_id_as_message (req_client_server_1, reqids_1_to_2::_1);
  send_request_id_as_message (req_client_server_2, reqids_2_to_1::_0);
  send_request_id_as_message (req_client_server_2, reqids_2_to_1::_1);
  send_request_id_as_message (req_client_server_2, reqids_2_to_1::_0);
  send_request_id_as_message (req_client_server_2, reqids_2_to_1::_1);
  send_request_id_as_message (req_client_server_2, reqids_2_to_1::_0);

  sockdir_1_to_2.wait_for_all_messages ();
  sockdir_2_to_1.wait_for_all_messages ();

  req_client_server_1.stop_thread ();
  req_client_server_2.stop_thread ();

  require_all_sent_requests_are_handled ();

  clear_socket_directions ();
}

static void
init_globals ()
{
  global_sent_request_count = 0;
  global_handled_request_count = 0;
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

  INFO ("Handle value: " << ((int) readval));
}

template <typename ReqCl, typename ReqId>
static void
send_request_id_as_message (ReqCl &reqcl, ReqId rid)
{
  reqcl.send (rid, static_cast<int> (rid));
  ++global_sent_request_count;
}

test_request_client
create_request_client ()
{
  cubcomm::channel chncl;
  chncl.set_channel_name ("client");

  test_request_client req_cl (std::move (chncl));

  return req_cl;
}

test_request_server
create_request_server ()
{
  cubcomm::channel chnsr;
  chnsr.set_channel_name ("server");

  test_request_server req_sr (std::move (chnsr));

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

  return req_sr;
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

//
// Mock CUBRID stuff
//
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