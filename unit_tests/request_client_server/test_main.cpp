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

template <typename T, T Val>
void
mock_server_request_handle (cubpacking::unpacker &upk)
{
  T readval;
  upk.unpack_from_int (readval);
  REQUIRE (readval == Val);
}

TEST_CASE ("A client and a server", "[default]")
{
  enum class reqids
  {
    _0,
    _1
  };

  cubcomm::channel chncl;
  chncl.set_channel_name ("client");

  cubcomm::channel chnsr;
  chnsr.set_channel_name ("server");

  mock_socket_direction sockdir;

  add_socket_direction (chncl.get_channel_id (), chnsr.get_channel_id (), sockdir);

  cubcomm::request_client<reqids> reqcl (std::move (chncl));
  cubcomm::request_server<reqids> reqsr (std::move (chnsr));

  cubcomm::request_server<reqids>::server_request_handler reqh0 = [] (cubpacking::unpacker &upk)
  {
    mock_server_request_handle<reqids, reqids::_0> (upk);
  };
  cubcomm::request_server<reqids>::server_request_handler reqh1 = [] (cubpacking::unpacker &upk)
  {
    mock_server_request_handle<reqids, reqids::_1> (upk);
  };

  reqsr.register_request_handler (reqids::_0, reqh0);
  reqsr.register_request_handler (reqids::_1, reqh1);
  reqsr.start_thread ();

  reqcl.send (reqids::_0, (int) reqids::_0);
  reqcl.send (reqids::_1, (int) reqids::_1);

  sockdir.wait_for_all_messages ();
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