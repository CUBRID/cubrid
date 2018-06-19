#ifndef __CUB_SERVER_COMMUNICATION_CHANNEL_HPP
#define __CUB_SERVER_COMMUNICATION_CHANNEL_HPP

#include "communication_channel.hpp"

/* so that unique_ptr knows to "free (char *)" and not to "delete char *" */
struct c_free_deleter
{
  void operator() (char *ptr)
  {
    if (ptr != NULL)
      {
	free (ptr);
      }
  }
};

class cub_server_communication_channel : public communication_channel
{
  public:
    cub_server_communication_channel (const char *server_name = "NO NAME", int max_timeout_in_ms = -1);
    ~cub_server_communication_channel () = default;

    cub_server_communication_channel (const cub_server_communication_channel &) = delete;
    cub_server_communication_channel &operator= (const cub_server_communication_channel &) = delete;

    cub_server_communication_channel (cub_server_communication_channel &&comm);
    cub_server_communication_channel &operator= (cub_server_communication_channel &&comm);

    css_error_code connect (const char *hostname, int port) override;

  private:
    std::unique_ptr <char, c_free_deleter> m_server_name;
    unsigned short m_request_id;
    enum css_command_type m_command_type;
};


#endif /* __CUB_SERVER_COMMUNICATION_CHANNEL_HPP */
