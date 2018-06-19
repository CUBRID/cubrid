#ifndef __CUB_SERVER_COMMUNICATION_CHANNEL_HPP
#define __CUB_SERVER_COMMUNICATION_CHANNEL_HPP

#include "communication_channel.hpp"

#include <string>

class cub_server_communication_channel : public communication_channel
{
  public:
    cub_server_communication_channel (const char *server_name, int max_timeout_in_ms = -1);
    ~cub_server_communication_channel () = default;

    cub_server_communication_channel (const cub_server_communication_channel &) = delete;
    cub_server_communication_channel &operator= (const cub_server_communication_channel &) = delete;

    cub_server_communication_channel (cub_server_communication_channel &&comm);
    cub_server_communication_channel &operator= (cub_server_communication_channel &&comm);

    css_error_code connect (const char *hostname, int port) override;

  private:
    std::string m_server_name;
    std::size_t m_server_name_length;
};

#endif /* __CUB_SERVER_COMMUNICATION_CHANNEL_HPP */
