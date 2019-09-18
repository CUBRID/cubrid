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

#ifndef _LOCKFREE_TRANSACTION_DESCRIPTOR_HPP_
#define _LOCKFREE_TRANSACTION_DESCRIPTOR_HPP_

#include "lockfree_transaction_def.hpp"

namespace lockfree
{
  namespace tran
  {
    class descriptor
    {
      public:
	// todo: make private
	id last_cleanup_id;   /* last ID for which a cleanup of retired_list was performed */
	id transaction_id;    /* id of current transaction */

	bool did_incr;        /* Was transaction ID incremented? */
    };
  } // namespace tran
} // namespace lockfree

#endif // !_LOCKFREE_TRANSACTION_DESCRIPTOR_HPP_
