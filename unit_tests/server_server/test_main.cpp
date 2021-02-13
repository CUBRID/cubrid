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

#include "communication_channel.hpp"
#include "server_server.hpp"

#include <cstring>
#include "stdio.h"

void ats_foo ();
void ps_foo ();
int
main (int, char **)
{
  printf ("asdf\n");
  ats_foo();
}

//////////////////////////////////////////////////////////////////////////

std::function<void (const char *, size_t)> net_pgbuf_read_page = [](const char *name, size_t len)
{
  printf ("read_page %s %lu\n", name, len);
};

std::function<void (const char *, size_t)> net_ps_get_page = [](const char *name, size_t len)
{
  printf ("get_page %s %lu\n", name, len);
};

void ats_foo ()
{
  // on active transaction server
  cubcomm::channel chn;  // create a channel
  using ats_to_ps_server_type = cubcomm::request_client_server<msgid_ats_to_ps, msgid_ps_to_ats>;
  ats_to_ps_server_type ats_server_with_ps (std::move (chn));

  ats_server_with_ps.register_request_handler (msgid_ps_to_ats::SEND_DATA_PAGE, net_pgbuf_read_page);
  // ...

  ats_server_with_ps.send (msgid_ats_to_ps::REQUEST_DATA_PAGE, "a vpid", std::strlen ("a vpid"));
}

void ps_foo ()
{
  // on page server
  cubcomm::channel chn; // accepted channel
  cubcomm::request_client_server<msgid_ps_to_ats, msgid_ats_to_ps> ps_server_with_ats (std::move (chn));

  ps_server_with_ats.register_request_handler (msgid_ats_to_ps::REQUEST_DATA_PAGE, net_ps_get_page);
  // ...
}

