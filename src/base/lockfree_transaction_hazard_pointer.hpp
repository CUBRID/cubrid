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
// lock-free transaction hazard pointer
//
//    Hazard pointers (https://en.wikipedia.org/wiki/Hazard_pointer) are pointers to memory used by lock-free data
//    structures. When hazard pointers are "deleted", they are passed to the lock-free transaction system that can
//    determine when it's safe to delete the pointer without impacting concurrent transaction reading the pointed
//    memory
//

#ifndef _LOCKFREE_TRANSACTION_HAZARD_POINTER_HPP_
#define _LOCKFREE_TRANSACTION_HAZARD_POINTER_HPP_

#include "lockfree_transaction_def.hpp"

namespace lockfree
{
  namespace tran
  {
    class hazard_pointer
    {
      public:
	hazard_pointer () = default;
	virtual ~hazard_pointer () = 0;   // to force abstract class

	virtual void on_retire ()
	{
	  // default is to delete itself
	  delete this;
	}

	id get_delete_id ()
	{
	  return m_delete_id;
	}
	void set_delete_id (id trid)
	{
	  m_delete_id = trid;
	}
	void set_hazard_link (hazard_pointer *next)
	{
	  m_hazard_next = next;
	}
	hazard_pointer *get_hazard_next ()
	{
	  return m_hazard_next;
	}

      protected:
	hazard_pointer *m_hazard_next;    // may be reused by derived classes

      private:
	id m_delete_id;
    };
  } // namespace tran
} // namespace lockfree

#endif // !_LOCKFREE_TRANSACTION_HAZARD_POINTER_HPP_
