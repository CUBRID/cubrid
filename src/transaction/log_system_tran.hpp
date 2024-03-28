/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
