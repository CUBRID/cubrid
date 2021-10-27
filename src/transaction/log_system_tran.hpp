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

//
// System transactions - can make changes to storage without modifying the database view; it requires logging.
//

#ifndef _LOG_SYSTEM_TRAN_HPP_
#define _LOG_SYSTEM_TRAN_HPP_

#include "storage_common.h"       // TRANID
#include "log_lsa.hpp"

#include <functional>

struct log_tdes;
struct log_lsa;

class log_system_tdes
{
  public:
    log_system_tdes ();
    log_system_tdes (log_tdes *tdes);
    log_system_tdes (const log_system_tdes &o) = delete;

    ~log_system_tdes ();

    log_tdes *get_tdes ();

    void on_sysop_start ();
    void on_sysop_end ();

    static void init_system_transations ();
    static void destroy_system_transactions ();

    using map_func = std::function<void (log_tdes &)>;
    using rv_delete_if_func = std::function<bool (const log_tdes &)>;

    static log_tdes *rv_get_or_alloc_tdes (TRANID trid, const LOG_LSA &log_lsa);
    static log_tdes *rv_get_tdes (TRANID trid);
    static void map_all_tdes (const map_func &func);

    static void rv_delete_all_tdes_if (const rv_delete_if_func &func);
    static void rv_delete_tdes (TRANID trid);
    static void rv_simulate_system_tdes (TRANID trid);
    static void rv_end_simulation ();
    static void rv_final ();

  private:
    log_tdes *m_tdes;
};

#endif // _LOG_SYSTEM_TRAN_HPP_
