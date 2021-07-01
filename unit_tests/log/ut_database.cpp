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

#include "ut_database.hpp"

#include <limits>

/*******************************************************
 * ut_database_values_generator
 *******************************************************/

ut_database_values_generator::ut_database_values_generator (const ut_database_config &a_database_config)
  : m_database_config (a_database_config)
  , m_gen (m_rd ())
    //, m_log_lsa () // the log_lsa ctor actually does not set any value
  , m_rand_log_lsa_dist (0.9)
  , m_duration_in_millis_dist (0., a_database_config.max_duration_in_millis)
  // *INDENT-OFF*
  , m_add_or_update_volume_dist ({ut_database_config::ADD_VOLUME_DISCRETE_RATIO,
                                 ut_database_config::UPDATE_VOLUME_DISCRETE_RATIO })
  , m_add_or_update_page_dist ({ut_database_config::ADD_PAGE_DISCRETE_RATIO,
                               ut_database_config::UPDATE_PAGE_DISCRETE_RATIO})
  // *INDENT-ON*
{
  // the log_lsa ctor actually does not set any value - and better not touch it
  // values must be explicitely set
  m_log_lsa.pageid = 10101; // arbitrary
  m_log_lsa.offset = 0;
}

const log_lsa &ut_database_values_generator::increment_and_get_lsa_log ()
{
  const auto if_false_increment_page_id_else_increment_offset = m_rand_log_lsa_dist (m_gen);
  if (if_false_increment_page_id_else_increment_offset == false)
    {
      // increment page id
      REQUIRE (m_log_lsa.pageid < MAX_LOG_LSA_PAGEID);
      ++m_log_lsa.pageid;
      m_log_lsa.offset = 0;
    }
  else
    {
      // increment offset
      if (m_log_lsa.offset == MAX_LOG_LSA_OFFSET)
	{
	  ++m_log_lsa.pageid;
	  m_log_lsa.offset = 0;
	}
      else
	{
	  ++m_log_lsa.offset;
	}
    }

  return m_log_lsa;
}

double ut_database_values_generator::rand_duration_in_millis ()
{
  if (m_database_config.max_duration_in_millis > 0)
    {
      const auto res = m_duration_in_millis_dist (m_gen);
      return res;
    }
  return 0;
}

ut_database_values_generator::add_or_update
ut_database_values_generator::rand_add_or_update_volume (const size_t a_current_volume_count)
{
  if (a_current_volume_count < m_database_config.max_volume_count_per_database)
    {
      // while there is still space to add volumes, choose randomly
      const int32_t random_value = m_add_or_update_volume_dist (m_gen);
      return (random_value == 0)
	     ? add_or_update::ADD
	     : add_or_update::UPDATE;
    }
  else
    {
      // once the maximum number of volumes has been reached, just update
      return add_or_update::UPDATE;
    }
}

size_t ut_database_values_generator::rand_index_of_entity_to_update (const size_t a_current_entity_count)
{
  const size_t res = (rand () * rand ()) % a_current_entity_count;
  return res;
}

ut_database_values_generator::add_or_update
ut_database_values_generator::rand_add_or_update_page (const size_t a_current_page_count)
{
  if (a_current_page_count < m_database_config.max_page_count_per_volume)
    {
      // while there is still space to add pages, choose randomly
      const int32_t random_value = m_add_or_update_page_dist (m_gen);
      return (random_value == 0)
	     ? add_or_update::ADD
	     : add_or_update::UPDATE;
    }
  else
    {
      // once the maximum number of pages per volume has been reached, just update
      return add_or_update::UPDATE;
    }
}

/*******************************************************
 * ut_page
 *******************************************************/

ut_page::ut_page (short a_volid, int32_t a_pageid)
  : m_vpid {a_pageid, a_volid}
{
  REQUIRE (a_volid != NULL_VOLID);
  REQUIRE (a_pageid != NULL_PAGEID);
}

const VPID &ut_page::get_vpid () const
{
  return m_vpid;
}

ux_ut_redo_job_impl ut_page::generate_changes (ut_database &a_database_recovery,
    ut_database_values_generator &a_db_global_values)
{
  const log_lsa log_lsa_v = a_db_global_values.increment_and_get_lsa_log (); // get a copy
  const double millis = a_db_global_values.rand_duration_in_millis ();

  ux_ut_redo_job_impl job_to_append
  {
    new ut_redo_job_impl (a_database_recovery, ut_redo_job_impl::job_type::ALTER_PAGE,
			  log_lsa_v, m_vpid, millis)
  };
  m_entries.push_front (std::move (job_to_append));

  ux_ut_redo_job_impl job_to_return
  {
    new ut_redo_job_impl (a_database_recovery, ut_redo_job_impl::job_type::ALTER_PAGE,
			  log_lsa_v, m_vpid, millis)
  };
  return job_to_return;
}

void ut_page::apply_changes (ux_ut_redo_job_impl &&a_job)
{
  // NOTE: although function is to be called from different threads, the function
  // is intentionally not locked as the intention is to check proper synchronization in
  // the redo log apply algorithm
  std::lock_guard<std::mutex> lock (m_apply_changes_mtx);

  m_entries.push_front (std::move (a_job));
}

void ut_page::require_equal (const ut_page &that) const
{
  auto this_entries_it = m_entries.cbegin ();
  auto that_entries_it = that.m_entries.cbegin ();
  while (this_entries_it != m_entries.cend () && that_entries_it != that.m_entries.cend ())
    {
      (*this_entries_it)->require_equal (**that_entries_it);
      ++this_entries_it;
      ++that_entries_it;
    }
  REQUIRE (this_entries_it == m_entries.cend ());
  REQUIRE (that_entries_it == that.m_entries.cend ());
}

/*******************************************************
 * ut_volume
 *******************************************************/

ut_volume::ut_volume (const ut_database_config &a_database_config, short a_volid)
  : m_database_config (a_database_config), m_volid (a_volid)
{
  REQUIRE (a_volid != NULL_VOLID);
  // start a volume without any pages
}

short ut_volume::get_volid () const
{
  return m_volid;
}

ux_ut_redo_job_impl ut_volume::generate_changes (ut_database &a_database_recovery,
    ut_database_values_generator &a_db_global_values)
{
  auto add_or_update_page = ut_database_values_generator::add_or_update::ADD;
  if (m_pages.size () > 0)
    {
      add_or_update_page = a_db_global_values.rand_add_or_update_page (m_pages.size ());
    }

  if (add_or_update_page == ut_database_values_generator::add_or_update::UPDATE)
    {
      // invoke existing pages to generate changes
      const int page_index = a_db_global_values.rand_index_of_entity_to_update (m_pages.size ());
      const auto &page = m_pages.at (page_index);
      return page->generate_changes (a_database_recovery, a_db_global_values);
    }
  else
    {
      if (add_or_update_page == ut_database_values_generator::add_or_update::ADD)
	{
	  // add new page and generate log entry
	  // akin to 'extend volume' operation
	  const log_lsa log_lsa_v = a_db_global_values.increment_and_get_lsa_log (); // get a copy
	  const double millis = a_db_global_values.rand_duration_in_millis ();
	  ux_ut_redo_job_impl job
	  {
	    new ut_redo_job_impl (a_database_recovery, ut_redo_job_impl::job_type::NEW_PAGE,
	    log_lsa_v, {static_cast<int32_t> (m_pages.size ()), m_volid}, millis)
	  };
	  add_new_page (m_pages);
	  return job;
	}
      else
	{
	  assert (false);
	}
    }
  return nullptr;
}

void ut_volume::apply_changes (ux_ut_redo_job_impl &&a_job)
{
  // NOTE: although function is to be called from different threads, the function
  // is intentionally not locked as the intention is to check proper synchronization in
  // the redo log apply algorithm
  if (a_job->is_volume_creation ())
    {
      assert (false);
    }
  else
    {
      if (a_job->is_page_creation ())
	{
	  std::lock_guard<std::mutex> lock (m_apply_changes_mtx);
	  const ux_ut_page &new_page = add_new_page (m_pages);
	  REQUIRE (new_page->get_vpid ().volid == a_job->get_vpid ().volid);
	}
      else
	{
	  std::lock_guard<std::mutex> lock (m_apply_changes_mtx);
	  ux_ut_page &page = m_pages.at (a_job->get_vpid ().pageid);
	  page->apply_changes (std::move (a_job));
	}
    }
}

void ut_volume::require_equal (const ut_volume &that) const
{
  REQUIRE (m_volid == that.m_volid);
  REQUIRE (m_pages.size () == that.m_pages.size ());

  auto this_pages_it = m_pages.cbegin ();
  auto that_pages_it = that.m_pages.cbegin ();
  while (this_pages_it != m_pages.cend () && that_pages_it != that.m_pages.cend ())
    {
      (*this_pages_it)->require_equal (**that_pages_it);
      ++this_pages_it;
      ++that_pages_it;
    }
}

const ux_ut_page &ut_volume::add_new_page (std::vector<ux_ut_page> &a_pages)
{
  ux_ut_page page { new ut_page (m_volid, a_pages.size ()) };
  a_pages.push_back (std::move (page));
  return *a_pages.rbegin ();
}

/*******************************************************
 * ut_database
 *******************************************************/

ut_database::ut_database (const ut_database_config &a_database_config)
  : m_database_config (a_database_config)
{
  // start without any volumes
}

ux_ut_redo_job_impl ut_database::generate_changes (ut_database &a_database_recovery,
    ut_database_values_generator &a_db_global_values)
{
  //const int update_or_add_volume = rand_update_or_add_volume();
  auto add_or_update_volume = ut_database_values_generator::add_or_update::ADD;
  if (m_volumes.size () > 0)
    {
      add_or_update_volume = a_db_global_values.rand_add_or_update_volume (m_volumes.size ());
    }

  if (add_or_update_volume == ut_database_values_generator::add_or_update::UPDATE)
    {
      // invoke existing volume to generate changes
      const int vol_index = a_db_global_values.rand_index_of_entity_to_update (m_volumes.size ());
      const auto &vol = m_volumes.at (vol_index);
      return vol->generate_changes (a_database_recovery, a_db_global_values);
    }
  else
    {
      if (add_or_update_volume == ut_database_values_generator::add_or_update::ADD)
	{
	  // add new volume and generate log entry
	  const log_lsa log_lsa_v = a_db_global_values.increment_and_get_lsa_log (); // get a copy
	  const double millis = a_db_global_values.rand_duration_in_millis ();
	  ux_ut_redo_job_impl job
	  {
	    // the value for page id is dummy and will not be used by this instance
	    new ut_redo_job_impl (a_database_recovery, ut_redo_job_impl::job_type::NEW_VOLUME,
	    log_lsa_v, {0, static_cast<short> (m_volumes.size ())}, millis)
	  };
	  add_new_volume (m_volumes);
	  return job;
	}
      else
	{
	  assert (false);
	}
    }
  return nullptr;
}

void ut_database::apply_changes (ux_ut_redo_job_impl &&a_job)
{
  // NOTE: although function is to be called from different threads, the function
  // is intentionally not locked as the intention is to check proper synchronization in
  // the redo log apply algorithm
  if (a_job->is_volume_creation ())
    {
      std::lock_guard<std::mutex> lock (m_apply_changes_mtx);
      const auto &new_volume = add_new_volume (m_volumes);
      REQUIRE (new_volume->get_volid () == a_job->get_vpid ().volid);
    }
  else
    {
      std::lock_guard<std::mutex> lock (m_apply_changes_mtx);
      REQUIRE ((a_job->is_page_creation () || a_job->is_page_modification ()));
      auto &volume = m_volumes.at (a_job->get_vpid ().volid);
      volume->apply_changes (std::move (a_job));
    }
}

void ut_database::require_equal (const ut_database &that) const
{
  REQUIRE (m_volumes.size () == that.m_volumes.size ());

  auto this_volumes_it = m_volumes.cbegin ();
  auto that_volumes_it = that.m_volumes.cbegin ();
  while (this_volumes_it != m_volumes.cend () && that_volumes_it != that.m_volumes.cend ())
    {
      (*this_volumes_it)->require_equal (**that_volumes_it);
      ++this_volumes_it;
      ++that_volumes_it;
    }
}

const ux_ut_volume &ut_database::add_new_volume (std::vector<ux_ut_volume> &a_volumes)
{
  ux_ut_volume vol { new ut_volume (m_database_config, a_volumes.size ()) };
  a_volumes.push_back (std::move (vol));
  return *a_volumes.rbegin ();
}
