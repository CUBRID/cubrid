#include "communication_channel.hpp"
#include "mem_block.hpp"
#include "packer.hpp"

#include <functional>
#include <map>
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

      static void loop_poll_and_receive (request_server *arg);
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

enum class msgid_ats_to_ps
{
  SEND_LOG_RECORD,
  REQUEST_DATA_PAGE,
  REQUEST_LOG_PAGE
};

enum class msgid_ps_to_ats
{
  SEND_DATA_PAGE,
  SEND_LOG_PAGE,
  SEND_START_LSA,
  SEND_SAVED_LSA
};


namespace cubcomm
{
  // --- request_client ---
  template <typename MsgId>
  template <typename ... PackableArgs>
  int request_client<MsgId>::send (MsgId msgid, const PackableArgs &... args)
  {
    packing_packer packer;
    cubmem::extensible_block eb;
    packer.set_buffer_and_pack_all(eb, msgid, args...);

    return m_channel.send(eb.get_ptr (), packer.get_current_size());
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
    m_thread = std::thread (request_server::loop_poll_and_receive, this);
  }

  template <typename MsgId>
  void request_server<MsgId>::stop_thread ()
  {
    m_shutdown = true;
    m_thread.join ();
  }

  template <typename MsgId>
  void request_server<MsgId>::loop_poll_and_receive (request_server *arg)
  {
    char rec_buffer[1024];
    MsgId msgid;
    while (!arg->m_shutdown)
      {
	size_t rec_len = 1024;
	css_error_code err = arg->m_channel.recv (rec_buffer, rec_len);
	if (err == NO_DATA_AVAILABLE)
	  {
	    continue;
	  }
	cubpacking::unpacker upk;
	upk.set_buffer (rec_buffer, rec_len);
	upk.unpack_int ((int &)msgid);
	assert(arg->m_request_handlers.count(msgid));
	arg->m_request_handlers[msgid](upk);
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

    const char *data = packer.get_buffer_start();
    int data_size = (int) packer.get_current_size ();

    return this->m_channel.send (data, data_size);
  }

}

