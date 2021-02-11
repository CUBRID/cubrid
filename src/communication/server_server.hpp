#include "communication_channel.hpp"
#include "packer.hpp"

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
      using server_request_handler = std::function<void (const char*, size_t)>;

      request_server () = delete;
      request_server (channel &&chn);
      ~request_server ();

      void start_thread ();
      void stop_thread ();

      void register_request_handler (MsgId msgid, server_request_handler& handler);

    private:
      using request_handlers_container = std::map<MsgId, server_request_handler>;

      void loop_poll_and_receive ();
      void handle (MsgId msgid, cubbase::unpacker &upkr);

      channel m_channel;
      std::thread m_thread;  // thread that loops poll & receive request
      request_handlers_container m_request_handlers;
  };

  // Both a client and a server. Client messages and server messages can have different specializations
  // Add 
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

