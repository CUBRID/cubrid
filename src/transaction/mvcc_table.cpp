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
// MVCC table - transaction information required for multi-version concurrency control system
//

#include "mvcc_table.hpp"

mvcc_trans_status::mvcc_trans_status ()
  : bit_area (NULL)
  , bit_area_start_mvccid (MVCCID_FIRST)
  , bit_area_length (0)
  , long_tran_mvccids (NULL)
  , long_tran_mvccids_length (0)
  , version (0)
  , lowest_active_mvccid (MVCCID_FIRST)
{
}

mvcctable::mvcctable ()
  : current_trans_status ()
  , transaction_lowest_active_mvccids (NULL)
  , trans_status_history (NULL)
  , trans_status_history_position (0)
#if defined (HAVE_ATOMIC_BUILTINS)
  , new_mvccid_lock PTHREAD_MUTEX_INITIALIZER
#endif
  , active_trans_mutex PTHREAD_MUTEX_INITIALIZER
{
}
