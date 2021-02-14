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

void ats_worker ();
void ps_worker ();

bool stop_ps = false;
bool stop_ats = false;

std::string pages[] = {"page0 aaaa", "page1 qwer", "page2 fdsa", "page333", "blaaaaasdfjlksfj"};

using ats_to_ps_server_type = cubcomm::request_client_server<msgid_ats_to_ps, msgid_ps_to_ats>;
using ps_to_ats_server_type = cubcomm::request_client_server<msgid_ps_to_ats, msgid_ats_to_ps>;


int main (int, char **)
{

  std::thread ps_th = std::thread (ps_worker);
  std::thread ats_th = std::thread (ats_worker);
  ats_th.join ();
  ps_th.join ();
  printf ("__main ps_th joined, finish.\n");
}

//////////////////////////////////////////////////////////////////////////

std::function<void (cubpacking::unpacker &upk)> net_pgbuf_read_page = [] (cubpacking::unpacker &upk)
{
  std::string s;
  upk.unpack_string (s);
  printf ("read_page %s\n", s.c_str ());
};

std::function<void (cubpacking::unpacker &upk)> net_ps_get_page = [] (cubpacking::unpacker &upk)
{
  int pg_no;
  upk.unpack_int (pg_no);
  printf ("get_page no. %d\n", pg_no);

//    ps_server_with_ats.send(msgid_ps_to_ats::SEND_DATA_PAGE, pages[pg_no]);
  stop_ats = true;
  stop_ps = true;
};


void ats_worker ()
{
  // on active transaction server
  cubcomm::channel ats_chn (100); // create a channel
  ats_to_ps_server_type ats_server_with_ps (std::move (ats_chn));

  ats_server_with_ps.register_request_handler (msgid_ps_to_ats::SEND_DATA_PAGE, net_pgbuf_read_page);
  // ...

  ats_server_with_ps.send (msgid_ats_to_ps::REQUEST_DATA_PAGE, 3);
  while (!stop_ats)
    {
      std::this_thread::sleep_for (std::chrono::seconds (1));
    }
}


void ps_worker ()
{
  // on page server

  cubcomm::channel ps_chn (101); // accepted channel
  ps_to_ats_server_type ps_server_with_ats (std::move (ps_chn));

  ps_server_with_ats.register_request_handler (msgid_ats_to_ps::REQUEST_DATA_PAGE, net_ps_get_page);
  ps_server_with_ats.start_thread ();
  printf ("thread started\n");
  while (!stop_ps)
    {
      std::this_thread::sleep_for (std::chrono::seconds (1));
    }
  ps_server_with_ats.stop_thread ();
  // ...
}

