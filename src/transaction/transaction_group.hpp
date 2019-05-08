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
// group of transactions class
//

#ifndef _TRANSACTION_GROUP_HPP_
#define _TRANSACTION_GROUP_HPP_

#include "extensible_array.hpp"
#include "log_comm.h"

#include <cstddef>
#include <vector>

// todo - namespace cubtx
class tx_group
{
  public:
    // forward declarations
    struct node_info
    {
      int m_tran_index;
      MVCCID m_mvccid;
      TRAN_STATE m_tran_state;

      node_info () = default;
      node_info (int tran_index, MVCCID mvccid, TRAN_STATE tran_state);
      ~node_info () = default;
    };

    using container_type = std::vector<node_info>;

    tx_group () = default;
    ~tx_group () = default;

    void add (const node_info &ni);
    void add (int tran_index, MVCCID mvccid, TRAN_STATE tran_state);
    const container_type &get_container () const;

    void transfer_to (tx_group &dest);

  private:
    container_type m_group;
};

#endif // !_TRANSACTION_GROUP_HPP_
