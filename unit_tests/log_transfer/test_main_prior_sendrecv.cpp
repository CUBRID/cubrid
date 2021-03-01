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

#include <vector>

class test_env
{
  public:
    test_env (size_t receivers_count);
    ~test_env ();

    void log_append ();
    void flush_log ();

  private:
    static void free_list (log_prior_node *headp);
    void require_prior_list_match () const;

    cublog::prior_sender m_sender;
    std::vector<log_prior_lsa_info *> m_dest_prior_infos;
    std::vector<cublog::prior_recver *> m_recvers;

    log_prior_lsa_info m_source_prior_info;

    log_prior_node *m_source_nodes_head = nullptr;
    log_prior_node *m_source_nodes_tail = nullptr;
};

TEST_CASE ("Test prior list transfers with a single receiver", "")
{
  test_env env (1);

  env.log_append ();
  env.flush_log ();

  env.flush_log ();

  for (size_t i = 0; i < 3; ++i)
    {
      env.log_append ();
    }
  env.flush_log ();
}

TEST_CASE ("Test prior list transfers with two receivers", "")
{
  test_env env (2);

  env.log_append ();
  env.flush_log ();

  env.flush_log ();

  for (size_t i = 0; i < 3; ++i)
    {
      env.log_append ();
    }
  env.flush_log ();
}

test_env::test_env (size_t receivers_count)
{
  for (size_t i = 0; i < receivers_count; ++i)
    {
      // add new destination prior info
      m_dest_prior_infos.push_back (new log_prior_lsa_info ());

      // add new prior receiver that reconstructs destination prior info
      m_recvers.push_back (new cublog::prior_recver (*m_dest_prior_infos.back ()));

      // add new sink for prior receiver
      m_sender.add_sink (std::bind (&cublog::prior_recver::push_message, std::ref (*m_recvers.back ()),
				    std::placeholders::_1));
    }
}

test_env::~test_env ()
{
  free_list (m_source_nodes_head);
  free_list (m_source_prior_info.prior_list_header);

  for (size_t i = 0; i < m_recvers.size (); ++i)
    {
      delete m_recvers[i];
      delete m_dest_prior_infos[i];
    }
}

void
test_env::log_append ()
{
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
test_env::flush_log ()
{
  m_sender.send_list (m_source_prior_info.prior_list_header);

  // pull list
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

// Add mock definitions for used CUBRID stuff
log_prior_lsa_info::log_prior_lsa_info () = default;

void
log_prior_lsa_info::push_list (log_prior_node *&list_head)
{
  log_prior_node *list_tail = list_head;

  while (list_tail->next != nullptr)
    {
      list_tail = list_tail->next;
    }

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
  std::string serialized;
  for (const log_prior_node *nodep = head; nodep != nullptr; nodep = nodep->next)
    {
      serialized.append (reinterpret_cast<const char *> (&nodep->start_lsa), sizeof (log_lsa));
    }
  return std::move (serialized);
}

log_prior_node *
prior_list_deserialize (const std::string &str)
{
  const char *ptr = str.c_str ();
  const char *end_ptr = str.c_str () + str.size ();

  log_prior_node *headp = nullptr;
  log_prior_node *tailp = nullptr;

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

  return headp;
}

void
log_wakeup_log_flush_daemon ()
{
  // do nothing
}