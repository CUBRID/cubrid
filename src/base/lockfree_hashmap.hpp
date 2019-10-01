/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

//
// lock-free hash map structure
//

#ifndef _LOCKFREE_HASHMAP_HPP_
#define _LOCKFREE_HASHMAP_HPP_

#include "lock_free.h"                        // for lf_entry_descriptor
#include "lockfree_address_marker.hpp"
#include "lockfree_freelist.hpp"

#include <mutex>

namespace lockfree
{
  template <class Key, class T>
  class hashmap
  {
    public:
      // todo

    private:
      using link_type = address_marker<T>;
      using freelist_type = freelist<T>;

      link_type *m_buckets;
      size_t m_size;

      link_type *m_backbuffer;
      std::mutex m_backbuffer_mutex;

      lf_entry_descriptor &m_edesc;
  };
}

#endif // !_LOCKFREE_HASHMAP_HPP_
