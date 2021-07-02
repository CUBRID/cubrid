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

#ifndef _UT_DATABASE_HPP_
#define _UT_DATABASE_HPP_

// same module includes
#include "ut_redo_job_impl.hpp"

// other module includes
#include "catch2/catch.hpp"
#include "log_lsa.hpp"
#include "storage_common.h"

// standard includes
#include <forward_list>
#include <mutex>
#include <random>
#include <string>
#include <vector>

/*
 * database config values
 */
struct ut_database_config
{
  static constexpr int32_t ADD_VOLUME_DISCRETE_RATIO = 1;
  static constexpr int32_t UPDATE_VOLUME_DISCRETE_RATIO = 39;

  static constexpr int32_t ADD_PAGE_DISCRETE_RATIO = 1;
  static constexpr int32_t UPDATE_PAGE_DISCRETE_RATIO = 19;

  const size_t max_volume_count_per_database;
  const size_t max_page_count_per_volume;

  /* the maximum duration in millis for a job to busy-loop
   * if 0, no busy-loop at all
   */
  const double max_duration_in_millis;
};

/*
 * takes database config and generates different values for unit test database
 */
class ut_database_values_generator
{
  public:
    /* either add a new instance or update an existing instance of a certain entity
     */
    enum class add_or_update
    {
      ADD,
      UPDATE
    };

  public:
    ut_database_values_generator (const ut_database_config &a_database_config);

    const log_lsa &increment_and_get_lsa_log ();

    double rand_duration_in_millis ();

    add_or_update rand_add_or_update_volume (const size_t a_current_volume_count);

    /* uniform distribution for updating entities
     */
    size_t rand_index_of_entity_to_update (const size_t a_current_entity_count);

    add_or_update rand_add_or_update_page (const size_t a_current_page_count);

  private:
    const ut_database_config &m_database_config;

    std::random_device m_rd;
    std::mt19937 m_gen;

    // just a global ever increasing id
    log_lsa m_log_lsa;
    std::bernoulli_distribution m_rand_log_lsa_dist;

    // number of millis for a task to busy wait
    std::uniform_real_distribution<double> m_duration_in_millis_dist;

    // discrete distribution for adding new volumes or updating existing volumes
    std::discrete_distribution<int32_t> m_add_or_update_volume_dist;

    // discrete distribution for adding new pages or updating existing pages
    std::discrete_distribution<int32_t> m_add_or_update_page_dist;
};


/*
 * a page in a unit test database volume
 */
class ut_page final
{
  public:
    ut_page (short a_volid, int32_t a_pageid);

    ut_page (const ut_page & ) = delete;
    ut_page (      ut_page && ) = delete;

    ut_page &operator= (const ut_page &) = delete;
    ut_page &operator= (      ut_page && ) = delete;

  public:
    const VPID &get_vpid () const;

    ux_ut_redo_job_impl generate_changes (ut_database &a_database_recovery,
					  ut_database_values_generator &a_db_global_values);
    void apply_changes (ux_ut_redo_job_impl &&a_job);

    void require_equal (const ut_page &that) const;

  private:
    const VPID m_vpid;

    std::forward_list<ux_ut_redo_job_impl> m_entries;

    std::mutex m_apply_changes_mtx;
};
using ux_ut_page = std::unique_ptr<ut_page>;

/*
 * a volume in a unit test database
 */
class ut_volume final
{
  public:
    ut_volume (const ut_database_config &a_database_config, short a_volid);

    ut_volume (const ut_volume & ) = delete;
    ut_volume (      ut_volume && ) = delete;

    ut_volume &operator= (const ut_volume &) = delete;
    ut_volume &operator= (      ut_volume && ) = delete;

  public:
    short get_volid () const;

    ux_ut_redo_job_impl generate_changes (ut_database &a_database_recovery,
					  ut_database_values_generator &a_db_global_values);
    void apply_changes (ux_ut_redo_job_impl &&a_job);

    void require_equal (const ut_volume &that) const;

  private:
    const ux_ut_page &add_new_page (std::vector<ux_ut_page> &a_pages);

  private:
    const ut_database_config &m_database_config;

    const short m_volid = NULL_VOLID;

    // page index in vector == page id
    std::vector<ux_ut_page> m_pages;

    std::mutex m_apply_changes_mtx;
};
using ux_ut_volume = std::unique_ptr<ut_volume>;

/*
 * a unit test database mock-like structure with database -<> volume
 */
class ut_database final
{
  public:
    ut_database (const ut_database_config &a_database_config);

    ut_database (const ut_database & ) = delete;
    ut_database (      ut_database && ) = delete;

    ut_database &operator= (const ut_database &) = delete;
    ut_database &operator= (      ut_database && ) = delete;

  public:
    ux_ut_redo_job_impl generate_changes (ut_database &a_database_recovery,
					  ut_database_values_generator &a_db_global_values);
    void apply_changes (ux_ut_redo_job_impl &&a_job);

    void require_equal (const ut_database &that) const;

  private:
    const ux_ut_volume &add_new_volume (std::vector<ux_ut_volume> &a_volumes);

  private:
    const ut_database_config &m_database_config;

    // volume index in vector == volume id
    std::vector<ux_ut_volume> m_volumes;

    std::mutex m_apply_changes_mtx;
};
using ux_ut_database = std::unique_ptr<ut_database>;

#endif // ! _UT_DATABASE_HPP_
