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

#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include "log_prior_recv.hpp"
#include "log_prior_send.hpp"
#include "system_parameter.h"

#include <condition_variable>
#include <mutex>
#include <vector>

// test_wakeup_flush_tracker: track the calls of log_wakeup_log_flush_daemon by log receivers
//
// used to synchronize prior nodes on sender/receiver.
//
class test_wakeup_flush_tracker
{
  public:
    void increment_count ();				  // increment count on log_wakeup_log_flush_daemon calls
    void wait_until_count_and_reset (size_t wait_count);  // wait until the wait_count is reached and reset the counter

  private:
    size_t m_flush_count = 0;				  // current count
    std::mutex m_sync;					  // protect access on count
    std::condition_variable m_cv;			  // notify caller of wait_until_count_and_reset
};
test_wakeup_flush_tracker test_Flush_track;    // global tracker

// test_env: simulate a sender and one or more receivers and log generation, flush/transfer
//
class test_env
{
  public:
    // ctor/dtor
    test_env (size_t receivers_count);	  // construct one sender and receivers_count receivers
    ~test_env ();

    void append_log ();			  // simulate log append
    void flush_and_transfer_log ();	  // simulate log flush. log is transferred to the receivers in the process
    // and at the end prior lists are compared

  private:
    static void free_list (log_prior_node *headp);
    void require_prior_list_match () const;

    // Sender & source data
    cublog::prior_sender m_sender;			  // the log sender
    log_prior_lsa_info m_source_prior_info;		  // the prior info where append_log() appends nodes and the
    // source of log transfer during log_flush()
    log_prior_node *m_source_nodes_head = nullptr;	  // head of list with all nodes that have been transferred
    log_prior_node *m_source_nodes_tail = nullptr;	  // tail of list with all nodes that have been transferred

    std::vector<log_prior_lsa_info *> m_dest_prior_infos; // destination prior info, one for each receiver
    std::vector<cublog::prior_recver *> m_recvers;	  // log receivers

    std::vector<cublog::prior_sender::sink_hook_t> m_prior_sender_sinks;
};

void
do_test (test_env &env)
{
  // test:
  // - append one log record and flush/transfer
  // - append no log record and flush/transfer
  // - append multiple log records and flush/transfer

  env.append_log ();
  env.flush_and_transfer_log ();

  env.flush_and_transfer_log ();

  for (size_t i = 0; i < 3; ++i)
    {
      env.append_log ();
    }
  env.flush_and_transfer_log ();
}

TEST_CASE ("Test prior list transfers with a single receiver", "")
{
  test_env env (1);
  do_test (env);
}

TEST_CASE ("Test prior list transfers with two receivers", "")
{
  test_env env (3);
  do_test (env);
}

test_env::test_env (size_t receivers_count)
{
  // affirmative answers for debug parameters used in the context of this test
  prm_set_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER, true);

  // For each receiver, three steps must be done:
  //	1. creating a prior info, where log is transferred
  //	2. creating the log receiver
  //	3. hooking a sink on the sender that transfers the log to the receiver

  for (size_t i = 0; i < receivers_count; ++i)
    {
      // add new destination prior info
      m_dest_prior_infos.push_back (new log_prior_lsa_info ());

      // add new prior receiver that reconstructs destination prior info
      m_recvers.push_back (new cublog::prior_recver (*m_dest_prior_infos.back ()));

      // add new sink for prior receiver
      m_prior_sender_sinks.emplace_back (
	      std::bind (&cublog::prior_recver::push_message, std::ref (*m_recvers.back ()), std::placeholders::_1));
    }

  for (const auto &sink : m_prior_sender_sinks)
    {
      // hooks sinks on the sender
      m_sender.add_sink (sink);
    }
}

test_env::~test_env ()
{
  free_list (m_source_nodes_head);
  free_list (m_source_prior_info.prior_list_header);

  m_prior_sender_sinks.clear ();
  for (size_t i = 0; i < m_recvers.size (); ++i)
    {
      delete m_recvers[i];
      delete m_dest_prior_infos[i];
    }
}

void
test_env::append_log ()
{
  // simulate appending log. add new node to prior info

  log_prior_node *nodep = new log_prior_node ();
  nodep->start_lsa = m_source_prior_info.prior_lsa;

  m_source_prior_info.prior_lsa.pageid++;
  nodep->log_header.forw_lsa = m_source_prior_info.prior_lsa;
  m_source_prior_info.prev_lsa = nodep->start_lsa;

  if (m_source_prior_info.prior_list_header == nullptr)
    {
      m_source_prior_info.prior_list_header = nodep;
      m_source_prior_info.prior_list_tail = nodep;
    }
  else
    {
      m_source_prior_info.prior_list_tail->next = nodep;
      m_source_prior_info.prior_list_tail = nodep;
    }
  nodep->next = nullptr;
}

void
test_env::flush_and_transfer_log ()
{
  // simulate "flush", when log is transferred to receivers

  m_sender.send_list (m_source_prior_info.prior_list_header);

  if (m_source_prior_info.prior_list_header != nullptr)
    {
      // pull list from prior info and append to m_source_nodes_head/m_source_nodes_tail
      if (m_source_nodes_tail == nullptr)
	{
	  m_source_nodes_head = m_source_prior_info.prior_list_header;
	  m_source_nodes_tail = m_source_prior_info.prior_list_tail;
	}
      else
	{
	  m_source_nodes_tail->next = m_source_prior_info.prior_list_header;
	  m_source_nodes_tail = m_source_prior_info.prior_list_tail;
	}

      m_source_prior_info.prior_list_header = nullptr;
      m_source_prior_info.prior_list_tail = nullptr;

      // now make sure that all messages have been processed
      // note: if m_source_prior_info.prior_list_header == nullptr, no message is sent.
      test_Flush_track.wait_until_count_and_reset (m_dest_prior_infos.size ());
    }

  // check prior list in source matches the lists
  require_prior_list_match ();
}

void
test_env::free_list (log_prior_node *headp)
{
  for (log_prior_node *nodep = headp; nodep != nullptr;)
    {
      log_prior_node *save_next = nodep->next;
      delete nodep;
      nodep = save_next;
    }
}

void
test_env::require_prior_list_match () const
{
  // source and destination prior lists have to match
  // the source list is in m_source_nodes_head/m_source_nodes_tail
  // the destination lists are in the destination prior info's

  if (m_source_nodes_head == nullptr)
    {
      return;
    }
  for (auto &dest_prior_info : m_dest_prior_infos)
    {
      const log_prior_node *dest_node = dest_prior_info->prior_list_header;
      const log_prior_node *source_node = m_source_nodes_head;
      while (source_node != nullptr)
	{
	  REQUIRE (dest_node != nullptr);
	  REQUIRE (dest_node->start_lsa == source_node->start_lsa);

	  source_node = source_node->next;
	  dest_node = dest_node->next;
	}
      REQUIRE (dest_node == nullptr);
      REQUIRE (m_source_prior_info.prev_lsa == dest_prior_info->prev_lsa);
      REQUIRE (m_source_nodes_tail->start_lsa == dest_prior_info->prior_list_tail->start_lsa);
    }
}

//
// Add mock definitions for used CUBRID stuff
//
#include "error_manager.h"
#include "log_append.hpp"
#include "log_manager.h"
#include "system_parameter.h"

PGLENGTH db_Io_page_size = IO_DEFAULT_PAGE_SIZE;
PGLENGTH db_Log_page_size = IO_DEFAULT_PAGE_SIZE;

log_prior_lsa_info::log_prior_lsa_info () = default;

void
log_prior_lsa_info::push_list (log_prior_node *&list_head, log_prior_node *&list_tail)
{
  if (prior_list_header == nullptr)
    {
      prior_list_header = list_head;
      prior_list_tail = list_tail;
    }
  else
    {
      prior_list_tail->next = list_head;
      prior_list_tail = list_tail;
    }

  prev_lsa = prior_list_tail->start_lsa;
}

std::string
prior_list_serialize (const log_prior_node *head)
{
  // only start_lsa is used

  std::string serialized;
  for (const log_prior_node *nodep = head; nodep != nullptr; nodep = nodep->next)
    {
      serialized.append (reinterpret_cast<const char *> (&nodep->start_lsa), sizeof (log_lsa));
    }
  return serialized;
}

void
prior_list_deserialize (const std::string &str, log_prior_node *&headp, log_prior_node *&tailp)
{
  // only start_lsa is used

  const char *ptr = str.c_str ();
  const char *end_ptr = str.c_str () + str.size ();

  headp = nullptr;
  tailp = nullptr;

  while (ptr < end_ptr)
    {
      log_prior_node *nodep = new log_prior_node ();
      nodep->start_lsa = *reinterpret_cast<const log_lsa *> (ptr);
      ptr += sizeof (log_lsa);

      if (headp == nullptr)
	{
	  headp = tailp = nodep;
	}
      else
	{
	  tailp->next = nodep;
	  tailp = nodep;
	}
      nodep->next = nullptr;
    }
  assert (ptr == end_ptr);
}

void
test_wakeup_flush_tracker::increment_count ()
{
  std::unique_lock<std::mutex> ulock (m_sync);
  ++m_flush_count;
  ulock.unlock ();
  m_cv.notify_all ();
}

void
test_wakeup_flush_tracker::wait_until_count_and_reset (size_t wait_count)
{
  std::unique_lock<std::mutex> ulock (m_sync);
  m_cv.wait (ulock, [&wait_count, this]
  {
    return m_flush_count >= wait_count;
  });
  REQUIRE (m_flush_count == wait_count);
  m_flush_count = 0;
}

void
log_wakeup_log_flush_daemon ()
{
  test_Flush_track.increment_count ();
}

void
_er_log_debug (const char *file_name, const int line_no, const char *fmt, ...)
{
}
