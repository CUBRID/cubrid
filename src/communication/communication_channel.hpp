#ifndef _COMMUNICATION_CHANNEL_HPP
#define _COMMUNICATION_CHANNEL_HPP

#include <string>
#include <mutex>

class communication_channel
{
  public:
    communication_channel ();
    virtual ~communication_channel ();

    int send (int sock_fd, const std::string &message, int timeout);
    int send (int sock_fd, const char *message, int message_length, int timeout);
    int recv (int sock_fd, char *buffer, int &received_length, int timeout);
    int connect_to (const char *hostname, int port);

    static const int &get_max_timeout ();
  protected:
    static const int TCP_MAX_TIMEOUT_IN_MS;
  private:
};

#endif /* _REPLICATION_CHANNEL_HPP */
