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

struct log_tdes;

class log_system_tdes
{
  public:
    log_system_tdes () = default;
    ~log_system_tdes ();

    void claim_tdes ();
    void retire_tdes ();
    log_tdes *get_tdes ();

    void on_sysop_start ();

  private:
    log_tdes *m_tdes;
};

#endif _LOG_SYSTEM_TRAN_HPP_
