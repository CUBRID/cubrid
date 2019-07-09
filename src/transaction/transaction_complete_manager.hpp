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
// Manager of completed transactions
//

#ifndef _TRANSACTION_COMPLETE_MANAGER_HPP_
#define _TRANSACTION_COMPLETE_MANAGER_HPP_

#include "log_impl.h"
#include "transaction_group.hpp"

#include <cinttypes>

namespace cubtx
{
  //
  // group_complete_manager is the common interface used by transactions to wait for commit
  //
  class complete_manager
  {
    public:
      using id_type = std::uint64_t;

      static const id_type NULL_ID = 0;
      virtual ~complete_manager () = 0;

      virtual LOG_TRAN_COMPLETE_MANAGER_TYPE get_manager_type ();

      virtual id_type register_transaction (int tran_index, MVCCID mvccid, TRAN_STATE state) = 0;
      virtual void complete_mvcc (id_type id) = 0;
      virtual void complete_logging (id_type id) = 0;
      virtual void complete (id_type id) = 0;
  };
}

#endif // !_TRANSACTION_COMPLETE_MANAGER_HPP_
