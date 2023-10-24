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

#include "catch2/catch.hpp"

#include "comm_channel_mock.hpp"

#include "connection_defs.h"
#include "communication_channel.hpp"

#include <cstring>
#include <map>

/*
 * mock channel implementation message_exchanges setup by channel ids.
 */

std::mutex global_mutex;

// maps contain non-owning pointers
std::atomic_bool global_sockdirs_initialized = false;
std::map<std::string, mock_socket_direction *> global_sender_sockdirs;
std::map<std::string, mock_socket_direction *> global_receiver_sockdirs;

//
// message_exchange helper class
//

bool
mock_socket_direction::push_message (std::string &&str)
{
  std::unique_lock<std::mutex> ulock (m_mutex);
  if (m_disconnect)
    {
      return false;
    }
  m_messages.push (std::move (str));
  ++m_message_count;
  ulock.unlock ();
  m_condvar.notify_all ();
  return true;
}

bool
mock_socket_direction::peek_message (std::string &str)
{
  std::unique_lock<std::mutex> ulock (m_mutex);
  m_condvar.wait (ulock, [this]
  {
    return m_disconnect || (!m_frozen && !m_messages.empty ());
  });

  if (m_disconnect)
    {
      return false;
    }

  assert (str.empty ());
  str = m_messages.front ();

  ulock.unlock ();
  m_condvar.notify_all ();

  return true;
}

bool
mock_socket_direction::pull_message (std::string &str)
{
  std::unique_lock<std::mutex> ulock (m_mutex);
  m_condvar.wait (ulock, [this]
  {
    return m_disconnect || (!m_frozen && !m_messages.empty ());
  });

  if (m_disconnect)
    {
      return false;
    }

  str = std::move (m_messages.front ());
  m_messages.pop ();

  ulock.unlock ();
  m_condvar.notify_all ();

  return true;
}

bool
mock_socket_direction::has_message ()
{
  std::unique_lock<std::mutex> ulock (m_mutex);
  return !m_messages.empty ();
}

void
mock_socket_direction::disconnect ()
{
  std::unique_lock<std::mutex> ulock (m_mutex);
  m_disconnect = true;
  ulock.unlock ();
  m_condvar.notify_all ();
}

void
mock_socket_direction::wait_for_all_messages ()
{
  std::unique_lock<std::mutex> ulock (m_mutex);
  m_condvar.wait (ulock, [this]
  {
    return m_messages.empty () || m_disconnect;
  });
}

void
mock_socket_direction::wait_until_message_count (size_t count)
{
  std::unique_lock<std::mutex> ulock (m_mutex);
  m_condvar.wait (ulock, [this, &count]
  {
    return m_message_count >= count || m_disconnect;
  });
}

void
mock_socket_direction::freeze ()
{
  std::unique_lock<std::mutex> ulock (m_mutex);
  m_frozen = true;
}

void
mock_socket_direction::unfreeze ()
{
  std::unique_lock<std::mutex> ulock (m_mutex);
  m_frozen = false;
  ulock.unlock ();
  m_condvar.notify_all ();
}

void
add_socket_direction (const std::string &sender_id, const std::string &receiver_id,
		      mock_socket_direction &sockdir, bool last_one_to_be_initialized)
{
  global_sender_sockdirs.emplace (sender_id, &sockdir);
  global_receiver_sockdirs.emplace (receiver_id, &sockdir);
  if (last_one_to_be_initialized)
    {
      global_sockdirs_initialized.store (true);
    }
  else
    {
      global_sockdirs_initialized.store (false);
    }
}

void
disconnect_sender_socket_direction (const std::string &sender_id)
{
  const auto it = global_sender_sockdirs.find (sender_id);
  if (it != global_sender_sockdirs.cend())
    {
      it->second->disconnect ();
    }
}

void
disconnect_receiver_socket_direction (const std::string &receiver_id)
{
  const auto it = global_receiver_sockdirs.find (receiver_id);

  if (it != global_receiver_sockdirs.cend())
    {
      it->second->disconnect ();
    }
}

void
freeze_receiver_socket_direction (const std::string &receiver_id)
{
  const auto it = global_receiver_sockdirs.find (receiver_id);
  assert (it != global_receiver_sockdirs.cend());

  it->second->freeze ();
}

void
unfreeze_receiver_socket_direction (const std::string &receiver_id)
{
  const auto it = global_receiver_sockdirs.find (receiver_id);
  assert (it != global_receiver_sockdirs.cend());

  it->second->unfreeze ();
}

bool
does_receiver_socket_direction_have_message (const std::string &receiver_id)
{
  const auto it = global_receiver_sockdirs.find (receiver_id);
  assert (it != global_receiver_sockdirs.cend());

  return it->second->has_message ();
}

void
clear_socket_directions ()
{
  global_sender_sockdirs.clear ();
  global_receiver_sockdirs.clear ();
  global_sockdirs_initialized.store (false);
}

namespace cubcomm
{
  //
  // channel mockup
  //

  std::atomic<uint64_t> channel::unique_id_allocator = 0;

  channel::channel (int max_timeout_in_ms)
    : m_max_timeout_in_ms (max_timeout_in_ms)
    , m_type (CHANNEL_TYPE::NO_TYPE)
    , m_socket (INVALID_SOCKET)
    , m_unique_id (unique_id_allocator++)
  {
  }

  channel::channel (std::string &&channel_name)
    : m_channel_name { std::move (channel_name) }
    , m_unique_id (unique_id_allocator++)
  {
  }

  channel::channel (int max_timeout_in_ms, std::string &&channel_name)
    : m_max_timeout_in_ms (max_timeout_in_ms)
    , m_channel_name { std::move (channel_name) }
    , m_unique_id (unique_id_allocator++)
  {
  }

  channel::channel (channel &&comm)
    : m_max_timeout_in_ms (comm.m_max_timeout_in_ms)
    , m_unique_id (comm.m_unique_id)
  {
    m_type = comm.m_type;
    comm.m_type = NO_TYPE;

    m_socket = comm.m_socket;
    comm.m_socket = INVALID_SOCKET;

    m_channel_name = std::move (comm.m_channel_name);
    m_hostname = std::move (comm.m_hostname);

    m_port = comm.m_port;
    comm.m_port = INVALID_PORT;
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
    // function is not used in unit test context anymore
    REQUIRE (false);
    return NO_ERRORS;
  }

  css_error_code channel::recv_allow_truncated (char *buffer, std::size_t &maxlen_in_recvlen_out,
      std::size_t &remaining_len)
  {
    const std::string channel_id = get_channel_id ();
    REQUIRE (global_receiver_sockdirs.find (channel_id) != global_receiver_sockdirs.cend ());

    std::string message_peek;

    remaining_len = 0;

    if (!global_receiver_sockdirs[channel_id]->peek_message (message_peek))
      {
	return CONNECTION_CLOSED;
      }

    REQUIRE (message_peek.size () > 0);
    if (message_peek.size () > maxlen_in_recvlen_out)
      {
	// copy at most what is supported and report back remaining
	// maxlen_in_recvlen_out - same length
	std::memcpy (buffer, message_peek.c_str (), maxlen_in_recvlen_out);

	remaining_len = message_peek.size () - maxlen_in_recvlen_out;

	// message will be consumed at completion/remainder
	return RECORD_TRUNCATED;
      }
    else
      {
	//REQUIRE (message.size () <= maxlen_in_recvlen_out);
	maxlen_in_recvlen_out = message_peek.size ();
	std::memcpy (buffer, message_peek.c_str (), maxlen_in_recvlen_out);

	std::string message_pull;
	if (!global_receiver_sockdirs[channel_id]->pull_message (message_pull))
	  {
	    REQUIRE (false);
	    return CONNECTION_CLOSED;
	  }
	assert (maxlen_in_recvlen_out == message_pull.size ());
	// message peek is now a dangling reference

	return NO_ERRORS;
      }
  }

  css_error_code channel::recv_remainder (char *buffer, std::size_t &maxlen_in_recvlen_out)
  {
    const std::string channel_id = get_channel_id ();
    REQUIRE (global_receiver_sockdirs.find (channel_id) != global_receiver_sockdirs.cend ());

    std::string message_pull;

    if (!global_receiver_sockdirs[channel_id]->pull_message (message_pull))
      {
	REQUIRE (false);
	return CONNECTION_CLOSED;
      }

    //maxlen_in_recvlen_out remains the same
    // TODO: how to actually read from remaining offset
    std::size_t str_offset = (message_pull.size () - maxlen_in_recvlen_out);
    const char *const str_at_offset = message_pull.c_str () + str_offset;
    std::memcpy (buffer, str_at_offset, maxlen_in_recvlen_out);

    return NO_ERRORS;
  }

  css_error_code channel::send (const char *buffer, std::size_t length)
  {
    const std::string channel_id = get_channel_id ();
    assert (global_sender_sockdirs.find (channel_id) != global_sender_sockdirs.cend ());

    if (!global_sender_sockdirs[channel_id]->push_message (std::string (buffer, length)))
      {
	return ERROR_ON_WRITE;
      }
    return NO_ERRORS;
  }

  css_error_code channel::send_int (int val)
  {
    return send (reinterpret_cast<const char *> (&val), sizeof (int));
  }

  css_error_code channel::recv_int (int &received)
  {
    size_t size = sizeof (received);

    return recv (reinterpret_cast<char *> (&received), size);
  }

  css_error_code channel::connect (const char *, int)
  {
    assert (0);
    return NO_ERRORS;
  }

  css_error_code channel::accept (SOCKET)
  {
    assert (0);
    return NO_ERRORS;
  }

  void channel::close_connection ()
  {
    disconnect_sender_socket_direction (get_channel_id ());
    disconnect_receiver_socket_direction (get_channel_id ());
  }

  int channel::get_max_timeout_in_ms () const
  {
    return m_max_timeout_in_ms;
  }

  int channel::wait_for (unsigned short int, unsigned short int &revents) const
  {
    std::string chnid = get_channel_id ();
    if (global_sockdirs_initialized.load () == false)
      {
	revents = 0;
      }
    else
      {
	assert (global_receiver_sockdirs.find (chnid) != global_receiver_sockdirs.cend ());
	if (global_receiver_sockdirs[chnid]->has_message ())
	  {
	    revents = POLLIN;
	  }
	else
	  {
	    revents = 0;
	  }
      }
    return 0;
  }

  bool channel::is_connection_alive () const
  {
    return !IS_INVALID_SOCKET (m_socket);
  }

} /* namespace cubcomm */
