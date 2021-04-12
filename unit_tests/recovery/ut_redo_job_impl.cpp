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
#include "ut_redo_job_impl.hpp"

#include "vpid.hpp"

#include <limits>

ut_redo_job_impl::ut_redo_job_impl (ut_database &a_database_recovery, job_type a_job_type,
				    const log_lsa &a_log_lsa_id, VPID a_vpid, double a_millis)
  : cublog::redo_parallel::redo_job_base (a_vpid, a_log_lsa_id)
  , m_database_recovery (a_database_recovery), m_job_type (a_job_type)
  , m_millis (a_millis)
{
}

int ut_redo_job_impl::execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
			       LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support)
{
  // busy wait before actually applying the changes as to simulate the real conditions
  if (m_millis > 0.)
    {
      busy_loop (m_millis);
    }

  auto my_clone = clone ();
  m_database_recovery.apply_changes (std::move (my_clone));

  return NO_ERROR;
}

template <typename T_FLOATING>
static bool close_equal (T_FLOATING left, T_FLOATING rite
			 , T_FLOATING eps = std::numeric_limits<T_FLOATING>::epsilon ())
{
  static_assert (std::is_floating_point<T_FLOATING>::value, "T_FLOATING must be floating point");

  const auto diff = std::fabs (left - rite);
  left = std::fabs (left);
  rite = std::fabs (rite);
  const auto largest_abs = (left > rite) ? left : rite;
  if (diff <= largest_abs * eps)
    {
      return true;
    }
  return false;
}

void ut_redo_job_impl::require_equal (const ut_redo_job_impl &that) const
{
  REQUIRE (get_log_lsa () == that.get_log_lsa ());
  REQUIRE (get_vpid () == that.get_vpid ());
  REQUIRE (close_equal (m_millis, that.m_millis));
}

bool ut_redo_job_impl::is_volume_creation () const
{
  return m_job_type == job_type::NEW_VOLUME;
}

bool ut_redo_job_impl::is_page_creation () const
{
  return m_job_type == job_type::NEW_PAGE;
}

bool ut_redo_job_impl::is_page_modification () const
{
  return m_job_type == job_type::ALTER_PAGE;
}

ux_ut_redo_job_impl ut_redo_job_impl::clone ()
{
  const auto &vpid = get_vpid ();
  const log_lsa &log_lsa = get_log_lsa ();
  ux_ut_redo_job_impl res { new ut_redo_job_impl (m_database_recovery, m_job_type, log_lsa, vpid, m_millis) };
  return res;
}

void ut_redo_job_impl::busy_loop (double a_millis)
{
  const auto start = std::chrono::system_clock::now ();
  int loop_count = 0;
  while (true)
    {
      // https://stackoverflow.com/a/58758133
      for (unsigned i = 0; i < 1000; i++)
	{
	  __asm__ __volatile__ ("" : "+g" (i) : :);
	}
      const std::chrono::duration<double, std::milli> diff_millis = std::chrono::system_clock::now () - start;
      const double diff_millis_count = diff_millis.count ();
      if (a_millis <= diff_millis_count )
	{
	  auto dbg = loop_count;
	  break;
	}
      ++loop_count;
    }
}
