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
    //////////////////////////////////////////////////////////////////////////
    //
    // Recovery contextual information that is either passed around between different recovery phases or that changes
    // the behavior of recovery.
    //
    //////////////////////////////////////////////////////////////////////////

  public:
    log_recovery_context ();

    log_recovery_context (const log_recovery_context &) = delete;
    log_recovery_context (log_recovery_context &&) = delete;

    // Accessors
    const log_lsa &get_checkpoint_lsa () const;
    const log_lsa &get_start_redo_lsa () const;
    const log_lsa &get_end_redo_lsa () const;
    void set_start_redo_lsa (const log_lsa &start_redo_lsa);
    void set_end_redo_lsa (const log_lsa &end_redo_lsa);

    // Restore related functions
    void init_for_recovery (const log_lsa &chkpt_lsa);
    void init_for_restore (const log_lsa &chkpt_lsa, const time_t *stopat_p);   // Init recovery context for restore
    bool is_restore_from_backup () const;                         // see m_is_restore_from_backup
    bool is_restore_incomplete () const;                          // see m_is_restore_incomplete
    void set_incomplete_restore ();                               // set m_is_restore_incomplete to true
    void set_forced_restore_stop ();                   // Force incomplete restoration and change stop time
    bool does_restore_stop_before_time (time_t complete_time);    // Is restore stopped before time argument

    // Page server
    bool is_page_server () const;

  private:
    // Restore related members
    time_t m_restore_stop_point = 0;	      // restore stop point. no stop point if the value is zero
    bool m_is_restore_from_backup = false;    // true if server is being restored restore from backup
    // false if server is recovering after forced stop
    bool m_is_restore_incomplete = false;     // true if restore is stopped before end of log
    // false if full restore is executed

    bool m_is_page_server = false;            // true for page server, false for transaction server

    log_lsa m_checkpoint_lsa = NULL_LSA;      // the initial checkpoint LSA, starting point for recovery analysis
    log_lsa m_start_redo_lsa = NULL_LSA;      // starting point for recovery redo
    log_lsa m_end_redo_lsa = NULL_LSA;        // end point for recovery redo
};
#endif // !_LOG_RECOVERY_CONTEXT_HPP_
