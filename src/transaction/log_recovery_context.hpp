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

#ifndef _LOG_RECOVERY_CONTEXT_HPP_
#define _LOG_RECOVERY_CONTEXT_HPP_

#include "log_lsa.hpp"

#include <time.h>

class log_recovery_context
{
    //

  public:
    log_recovery_context ();

    log_recovery_context (const log_recovery_context &) = delete;
    log_recovery_context (log_recovery_context &&) = delete;

    // Accessors
    void set_end_redo_lsa (const log_lsa &end_redo_lsa);
    void set_start_redo_lsa (const log_lsa &start_redo_lsa);
    log_lsa get_checkpoint_lsa () const;
    log_lsa get_start_redo_lsa () const;
    log_lsa get_end_redo_lsa () const;

    // Restore related functions
    void init_for_restore (const time_t *stopat_p);   // Init recovery context for restore
    bool is_restore_from_backup () const;
    bool is_restore_incomplete () const;              // Returns true if restore is set to stop before end of log
    void force_stop_restore_at (time_t stopat);       // Force incomplete restoration and change stop time
    bool does_restore_stop_before_time (time_t complete_time);
    void set_incomplete_restore ();

  private:
    time_t m_restore_stop_point = 0;	      // no stop point if the value is zero
    bool m_is_restore_from_backup = false;
    bool m_is_restore_incomplete = false;

    bool m_is_page_server = false;

    log_lsa m_checkpoint_lsa = NULL_LSA;
    log_lsa m_start_redo_lsa = NULL_LSA;
    log_lsa m_end_redo_lsa = NULL_LSA;
};
#endif // !_LOG_RECOVERY_CONTEXT_HPP_
