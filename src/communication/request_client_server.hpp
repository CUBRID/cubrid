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

#ifndef _SERVER_SERVER_HPP_
#define _SERVER_SERVER_HPP_

#include "communication_channel.hpp"
#include "error_manager.h"
#include "mem_block.hpp"
#include "packer.hpp"
#include "system_parameter.h"

#include <functional>
#include <map>
#include <memory>
#include <thread>

namespace cubcomm
{

  // A client that sends the specialized request messages
  template <typename MsgId>
  class request_client
  {
    public:
      request_client () = delete;
      request_client (channel &&chn);

      template <typename ... PackableArgs>
      int send (MsgId msgid, const PackableArgs &... args);

    private:
      channel m_channel;
  };

  // A server that handles request messages
  template <typename MsgId>
  class request_server
  {
    public:
      using server_request_handler = std::function<void (cubpacking::unpacker &upk)>;

      request_server () = delete;
      request_server (channel &&chn);
      ~request_server ();

      void start_thread ();
      void stop_thread ();

      void register_request_handler (MsgId msgid, server_request_handler &handler);

    protected:
      channel m_channel;

    private:
      using request_handlers_container = std::map<MsgId, server_request_handler>;

      void loop_poll_and_receive ();
      void handle (MsgId msgid, cubpacking::unpacker &upkr);

      std::thread m_thread;  // thread that loops poll & receive request
      bool m_shutdown;
      request_handlers_container m_request_handlers;
  };

  // Both a client and a server. Client messages and server messages can have different specializations
  template <typename ClientMsgId, typename ServerMsgId = ClientMsgId>
  class request_client_server : public request_server<ServerMsgId>
  {
    public:
      request_client_server (channel &&chn);

      template <typename ... PackableArgs>
      int send (ClientMsgId msgid, const PackableArgs &... args);
  };
}

namespace cubcomm
{
  // --- request_client ---
  template <typename MsgId>
  template <typename ... PackableArgs>
  int request_client<MsgId>::send (MsgId msgid, const PackableArgs &... args)
  {
    packing_packer packer;
    cubmem::extensible_block eb;
    packer.set_buffer_and_pack_all (eb, msgid, args...);

    int rc = m_channel.send_int (packer.get_current_size ());
    if (rc != NO_ERRORS)
      {
	return rc;
      }

    return m_channel.send (eb.get_ptr (), packer.get_current_size ());
  }

  // --- request_server ---
  template <typename MsgId>
  request_server<MsgId>::request_server (channel &&chn)
    : m_channel (std::move (chn))
  {
  }

  template <typename MsgId>
  void request_server<MsgId>::register_request_handler (MsgId msgid, server_request_handler &handler)
  {
    m_request_handlers[msgid] = handler;
  }

  template <typename MsgId>
  void request_server<MsgId>::start_thread ()
  {
    m_shutdown = false;
    m_thread = std::thread (&request_server::loop_poll_and_receive, std::ref (*this));
  }

  template <typename MsgId>
  void request_server<MsgId>::stop_thread ()
  {
    m_shutdown = true;
    m_thread.join ();
  }

  template <typename MsgId>
  void request_server<MsgId>::loop_poll_and_receive ()
  {
    MsgId msgid;
    while (!m_shutdown)
      {
	int ilen;
	size_t ulen;

	css_error_code err = m_channel.recv_int (ilen);
	if (err != NO_ERRORS)
	  {
	    er_log_debug (ARG_FILE_LINE, "error receiving length");
	    continue;
	  }
	std::unique_ptr<char[]> rec_buffer (new char (ilen));

	err = m_channel.recv (rec_buffer.get (), ulen);
	if (err == NO_DATA_AVAILABLE)
	  {
	    continue;
	  }
	if (err != NO_ERRORS)
	  {
	    er_log_debug (ARG_FILE_LINE, "error receiving message");
	    continue;
	  }

	cubpacking::unpacker upk;
	upk.set_buffer (rec_buffer.get (), ulen);
	upk.unpack_int ((int &)msgid);
	assert (m_request_handlers.count (msgid));
	m_request_handlers[msgid] (upk);
      }
  }

  template <typename MsgId>
  request_server<MsgId>::~request_server ()
  {
    m_shutdown = true;
    m_channel.close_connection ();
    if (m_thread.joinable ())
      {
	m_thread.join ();
      }
  }

  // --- request_client_server ---
  template <typename ClientMsgId, typename ServerMsgId>
  request_client_server<ClientMsgId, ServerMsgId>::request_client_server (channel &&chn)
    : request_server<ServerMsgId>::request_server (std::move (chn))
  {
  }

  template <typename ClientMsgId, typename ServerMsgId>
  template <typename ... PackableArgs>
  int request_client_server<ClientMsgId, ServerMsgId>::send (ClientMsgId msgid, const PackableArgs &... args)
  {

    packing_packer packer;
    cubmem::extensible_block eb;

    packer.set_buffer_and_pack_all (eb, (int) msgid, args...);

    const char *data = packer.get_buffer_start ();
    int data_size = (int) packer.get_current_size ();

    return this->m_channel.send (data, data_size);
  }

}

#endif // _SERVER_SERVER_HPP_
