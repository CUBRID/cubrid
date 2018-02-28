#ifndef _COMMUNICATION_CHANNEL_HPP
#define _COMMUNICATION_CHANNEL_HPP

#include <string>
#include <mutex>
#include "connection_defs.h"
#include <memory>

enum CHANNEL_TYPE
{
  INITIATOR = 0,
  LISTENER
};

class communication_channel
{
  public:
    communication_channel (const char *hostname, int port, CSS_COMMAND_TYPE command_type = NULL_REQUEST, const char *server_name = NULL, int max_timeout_in_ms = -1);
    communication_channel (int sock_fd = -1, int max_timeout_in_ms = -1);
    virtual ~communication_channel ();

    communication_channel (const communication_channel &comm) = delete;
    communication_channel &operator= (const communication_channel &comm) = delete;
    communication_channel (communication_channel &&comm);
    communication_channel &operator= (communication_channel &&comm);

    int recv (char *buffer, int &received_length);
    int send (const char *message, int message_length);
    int send (const std::string &message);
    virtual int connect ();
    int wait_for (unsigned short int events, unsigned short int &revents);
    bool is_connection_alive ();
    CSS_CONN_ENTRY *get_conn_entry ();

    void close_connection ();
    void set_command_type (CSS_COMMAND_TYPE cmd);

    const int &get_max_timeout_in_ms ();
  private:
    CSS_CONN_ENTRY *m_conn_entry;
    const int m_max_timeout_in_ms;
    std::unique_ptr <char> m_hostname, m_server_name;
    int m_port;
    unsigned short m_request_id;
    CHANNEL_TYPE m_type;
    CSS_COMMAND_TYPE m_command_type;
};

#endif /* _COMMUNICATION_CHANNEL_HPP */
