#ifndef __COMMUNICATION_SERVER_CHANNEL_HPP
#define __COMMUNICATION_SERVER_CHANNEL_HPP

#include "communication_channel.hpp"

#include <string>

namespace cubcomm {

  class server_channel : public channel
  {
    public:
      server_channel (const char *server_name, int max_timeout_in_ms = -1);
      ~server_channel () = default;

      server_channel (const server_channel &) = delete;
      server_channel &operator= (const server_channel &) = delete;

      server_channel (server_channel &&comm);
      server_channel &operator= (server_channel &&comm);

      css_error_code connect (const char *hostname, int port) override;

    private:
      std::string m_server_name;
      std::size_t m_server_name_length;
  };

}; /* cubcomm namepace */

#endif /* __COMMUNICATION_SERVER_CHANNEL_HPP */
