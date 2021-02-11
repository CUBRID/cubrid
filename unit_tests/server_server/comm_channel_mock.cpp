#include "connection_defs.h"
#include "communication_channel.hpp"

#include <condition_variable>
#include <queue>

namespace cubcomm
{
  struct ChMsg
  {
    std::size_t length;
    std::string message;
  };
//    std::shared_ptr<ChMsg>

  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::queue<std::shared_ptr<ChMsg>> m_queue;



  channel::channel (int max_timeout_in_ms)
    : m_max_timeout_in_ms (max_timeout_in_ms),
      m_type (CHANNEL_TYPE::NO_TYPE),
      m_socket (INVALID_SOCKET)
  {
  }

  channel::channel (channel &&comm)
    : m_max_timeout_in_ms (comm.m_max_timeout_in_ms)
  {
    m_type = comm.m_type;
    comm.m_type = NO_TYPE;

    m_socket = comm.m_socket;
    comm.m_socket = INVALID_SOCKET;
  }

  channel &channel::operator= (channel &&comm)
  {
    assert (!is_connection_alive ());
    this->~channel ();

    new (this) channel (std::move (comm));
    return *this;
  }

  channel::~channel ()
  {
    close_connection ();
  }

  css_error_code channel::send (const std::string &message)
  {
    return channel::send (message.c_str (), message.length ());
  }

  css_error_code channel::recv (char *buffer, std::size_t &maxlen_in_recvlen_out)
  {
    // Wait for a message to be added to the queue
    std::unique_lock<std::mutex> lk (m_mutex);
    while (m_queue.empty ())
      {
	m_cv.wait (lk);
      }

    if (m_queue.empty ())
      {
	return NO_DATA_AVAILABLE;
      }

    std::shared_ptr<ChMsg> msg = m_queue.front ();
    m_queue.pop ();
    return NO_ERRORS;
  }

  css_error_code channel::send (const char *buffer, std::size_t length)
  {
    std::shared_ptr<ChMsg> msg (new ChMsg {length, buffer});
    std::unique_lock<std::mutex> lk (m_mutex);
    m_queue.push (msg);
    m_cv.notify_one ();
    return NO_ERRORS;
  }

  bool channel::send_int (int val)
  {
    std::string s = std::to_string(val);
    return send(s.c_str(), s.length());
  }

  css_error_code channel::recv_int (int &received)
  {
    size_t len = sizeof (received);
    auto rc = recv ((char *) &received, len);
    if (rc != NO_ERRORS)
      {
	return rc;
      }

    if (len != sizeof (received))
      {
	return css_error_code::ERROR_ON_COMMAND_READ;
      }

//    received = ntohl (received);
    return NO_ERRORS;
  }

  css_error_code channel::connect (const char *hostname, int port)
  {
    if (is_connection_alive ())
      {
	assert (false);
	return INTERNAL_CSS_ERROR;
      }

    m_type = CHANNEL_TYPE::INITIATOR;


    return NO_ERRORS;
  }

  css_error_code channel::accept (SOCKET socket)
  {
    if (is_connection_alive () || IS_INVALID_SOCKET (socket))
      {
	return INTERNAL_CSS_ERROR;
      }

    m_type = CHANNEL_TYPE::LISTENER;
    m_socket = socket;

    return NO_ERRORS;
  }

  void channel::close_connection ()
  {
//    if (!IS_INVALID_SOCKET (m_socket))
//      {
//	css_shutdown_socket (m_socket);
//	m_socket = INVALID_SOCKET;
//	m_type = NO_TYPE;
//      }

    m_hostname = "";
    m_port = -1;
  }

  int channel::get_max_timeout_in_ms ()
  {
    return m_max_timeout_in_ms;
  }

  int channel::wait_for (unsigned short int events, unsigned short int &revents)
  {
//    POLL_FD poll_fd = {0, 0, 0};
    int rc = 0;
    revents = 0;

    if (!is_connection_alive ())
      {
	return -1;
      }

    assert (m_type != NO_TYPE);

//    poll_fd.fd = m_socket;
//    poll_fd.events = events;
//    poll_fd.revents = 0;

//    rc = css_platform_independent_poll (&poll_fd, 1, m_max_timeout_in_ms);
//    revents = poll_fd.revents;

    return rc;
  }

  bool channel::is_connection_alive () const
  {
    return !IS_INVALID_SOCKET (m_socket);
  }

//  SOCKET channel::get_socket ()
//  {
//    return m_socket;
//  }

} /* namespace cubcomm */
