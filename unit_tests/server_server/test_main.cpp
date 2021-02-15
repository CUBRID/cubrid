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
#include <functional>
#include <iostream>

std::string pages[] = {"page0 aaaa", "page1 qwer", "page2 fdsa", "page333", "blaaaaasdfjlksfj"};

using ats_to_ps_server_type = cubcomm::request_client_server<msgid_ats_to_ps, msgid_ps_to_ats>;
using ps_to_ats_server_type = cubcomm::request_client_server<msgid_ps_to_ats, msgid_ats_to_ps>;

int global_error = 0;

// active transaction server
// - sends a REQUEST_DATA_PAGE message (command) to page server
// - receives a SEND_DATA_PAGE message containing the data page
class at_server
{
  public:
    at_server (int timeout) :
      chn (timeout),
      ats_server_with_ps (std::move (chn)),
      shutdown (false)
    {}

    void do_stuff ()
    {
      cubcomm::request_server<msgid_ats_to_ps>::server_request_handler send_page =
	      std::bind (&at_server::net_pgbuf_read_page, std::ref (*this), std::placeholders::_1);

      ats_server_with_ps.register_request_handler (msgid_ps_to_ats::SEND_DATA_PAGE, send_page);
      ats_server_with_ps.start_thread ();
      th = std::thread (at_server::worker, this);
    }
    void stop ()
    {
      shutdown = true;
      th.join ();
    }

  private:
    cubcomm::channel chn;
    ats_to_ps_server_type ats_server_with_ps;
    std::thread th;
    bool shutdown;

    void net_pgbuf_read_page (cubpacking::unpacker &upk)
    {
      std::string s;
      upk.unpack_string (s);
      if (s != pages[3])
	{
	  global_error |= -2;
	}
    }

    static void worker (at_server *arg)
    {
      arg->ats_server_with_ps.send (msgid_ats_to_ps::REQUEST_DATA_PAGE, 3);
      while (!arg->shutdown)
	{
	  std::this_thread::sleep_for (std::chrono::seconds (1));
	}
    }
};

// page server
// - receives a REQUEST_DATA_PAGE message with page number from ATS
// - sends a SEND_DATA_PAGE message to PS containing the page
class p_server
{
  public:
    p_server (int timeout) :
      chn (timeout),
      ps_server_with_ats (std::move (chn)),
      shutdown (false)
    {}

    void do_stuff ()
    {
      cubcomm::request_server<msgid_ats_to_ps>::server_request_handler request_page =
	      std::bind (&p_server::net_ps_get_page, std::ref (*this), std::placeholders::_1);

      ps_server_with_ats.register_request_handler (msgid_ats_to_ps::REQUEST_DATA_PAGE, request_page);
      ps_server_with_ats.start_thread ();

      th = std::thread (p_server::worker, this);
    }
    void stop ()
    {
      shutdown = true;
      th.join ();
    }

  private:
    cubcomm::channel chn;
    ps_to_ats_server_type ps_server_with_ats;
    std::thread th;
    bool shutdown;

    void net_ps_get_page (cubpacking::unpacker &upk)
    {
      int pg_no;
      upk.unpack_int (pg_no);
      if (pg_no != 3)
	{
	  global_error |= -1;
	}

      ps_server_with_ats.send (msgid_ps_to_ats::SEND_DATA_PAGE, pages[pg_no]);

    };

    static void worker (p_server *arg)
    {
      while (!arg->shutdown)
	{
	  std::this_thread::sleep_for (std::chrono::seconds (1));
	}
    }
};

int main (int, char **)
{
  // init transaction (at) and page (p) servers with even/odd timeouts
  // so that we can differentiate them and chose the proper queue for each direction
  at_server ats (100);
  p_server ps (101);
  ps.do_stuff ();
  ats.do_stuff ();
  ats.stop ();
  ps.stop ();
  if (global_error)
    {
      std::cout << "  test failed" << std::endl;
    }
  else
    {
      std::cout << "  test completed successfully" << std::endl;
    }
  return global_error;
}

