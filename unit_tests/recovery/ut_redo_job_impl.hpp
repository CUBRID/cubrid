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

#ifndef _UT_REDO_JOB_IMPL_H_
#define _UT_REDO_JOB_IMPL_H_

// same module includes
#include "log_recovery_redo_parallel.hpp"

// forward declarations
class ut_database;

class ut_redo_job_impl;
using ux_ut_redo_job_impl = std::unique_ptr<ut_redo_job_impl>;

/*
 */
class ut_redo_job_impl final : public cublog::redo_parallel::redo_job_base
{
  public:
    /* we could rely on the actual value of the vpid to discriminate as to what
     * kind of job the instance is; however, in the base class - used in prod code -
     * there is an assert that the vpid must not be null; as such, we discriminate
     * the type of job using this enum; this also allows for more flexibility if,
     * later, we might add other mocking operations as well (eg: delete page/volume)
     */
    enum class job_type
    {
      NEW_VOLUME,
      NEW_PAGE,
      ALTER_PAGE,
    };

  public:
    ut_redo_job_impl (ut_database &a_database_recovery, job_type a_job_type,
		      INT64 a_id, VPID a_vpid, double a_millis);

    ut_redo_job_impl (ut_redo_job_impl const &) = delete;
    ut_redo_job_impl (ut_redo_job_impl &&) = delete;

    ~ut_redo_job_impl () override = default;

    ut_redo_job_impl &operator = (ut_redo_job_impl const &) = delete;
    ut_redo_job_impl &operator = (ut_redo_job_impl &&) = delete;

    int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
		 LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) override;

    void require_equal (const ut_redo_job_impl &that) const;

    bool is_volume_creation () const;
    bool is_page_creation () const;
    bool is_page_modification () const;

  private:
    /* hack to allow a clone of self for testing purposes
     */
    ux_ut_redo_job_impl clone ();

    static void busy_loop (double a_millis);

  private:
    ut_database &m_database_recovery;

    const job_type m_job_type;

    const INT64 m_id;
    const double m_millis;
};

#endif // ! _UT_REDO_JOB_IMPL_H_
