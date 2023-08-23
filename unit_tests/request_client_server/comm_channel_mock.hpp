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

#ifndef _COMM_CHANNEL_MOCK_HPP_
#define _COMM_CHANNEL_MOCK_HPP_

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

// use mock_socket_direction to mock-up the messages going in one direction through communication channel.
// for bi-directional messages, two mock_socket_direction instances are required.
//
// add_socket_direction function is used to register all sender/receiver communication channels. each communication
// channel operates on a mock_socket_direction when sending messages and on another mock_socket_direction when
// receiving messages.
//
// what to do to test one client and one server communication:
//  1) create the client and server channels and a mock_socket_direction instance
//  2) call add_socket_direction, with the next arguments:
//      - client channel ID as sender_id
//      - server channel ID as receiver_id
//      - the mock_socket_direction instance as sockdir
// The client channel will push messages to the mock_socket_direction on send/send_int and the server channel will
// consume the messages from the same mock_socket_direction.
//
// what to do to test two channels that can send/receive to/from each other:
//  1) create the client/server channels and two mock_socket_direction instances
//  2) call first add_socket_direction, with the next arguments:
//      - first channel ID as sender_id
//      - second channel ID as receiver_id
//      - first mock_socket_direction instance as sockdir
//  3) call second add_socket_direction, with the next arguments:
//      - second channel ID as sender_id
//      - first channel ID as receiver_id
//      - second mock_socket_direction instance as sockdir
// The first channel will push the messages on the first mock_socket_direction on send/send_int, and the second channel
// will consume the messages from the same mock_socket_direction.
// Vice-versa for the second mock_socket_direction.
//
class mock_socket_direction
{
  public:

    bool push_message (std::string &&str);      // push a message on channel send/send_int
    bool peek_message (std::string &str);       // wait until a message arrives and peek at it
    bool pull_message (std::string &str);       // wait until a message arrives and pop it on channel recv/recv_int
    bool has_message ();
    void disconnect ();                         // abort all waits when channels are disconnected
    void wait_for_all_messages ();              // wait until all messages are pulled and the message queue is empty
    void wait_until_message_count (size_t count);

    void freeze ();                             // Block to read a message to simulate a communication delay
    void unfreeze ();                           // Unblock to read a message to simulate a coomunication delay

  private:
    std::queue<std::string> m_messages;
    std::mutex m_mutex;
    std::condition_variable m_condvar;
    bool m_disconnect = false;
    size_t m_message_count = 0;
    bool m_frozen = false;
};

void add_socket_direction (const std::string &sender_id, const std::string &receiver_id,
			   mock_socket_direction &sockdir, bool last_one_to_be_initialized);
void disconnect_sender_socket_direction (const std::string &sender_id);
void disconnect_receiver_socket_direction (const std::string &receiver_id);
void freeze_receiver_socket_direction (const std::string &receiver_id);
void unfreeze_receiver_socket_direction (const std::string &receiver_id);
bool does_receiver_socket_direction_have_message (const std::string &receiver_id);
void clear_socket_directions ();

#endif // _COMM_CHANNEL_MOCK_HPP_
