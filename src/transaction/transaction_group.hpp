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
#include <iterator>

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

    class iterator;
    class const_iterator;

    tx_group () = default;
    ~tx_group () = default;

    void add (const node_info &ni);
    void add (int tran_index, MVCCID mvccid, TRAN_STATE tran_state);

    iterator begin () noexcept;
    const_iterator begin () const noexcept;
    const_iterator cbegin () const noexcept;
    iterator end () noexcept;
    const_iterator end () const noexcept;
    const_iterator cend () const noexcept;

    void transfer_to (tx_group &dest);

  private:
    static const size_t DEFAULT_GROUP_SIZE = 64;
    cubmem::appendable_array<node_info, DEFAULT_GROUP_SIZE> m_group;
};

class tx_group::iterator
{
  public:
    using difference_type = std::ptrdiff_t;
    using value_type = tx_group::node_info;
    using reference = tx_group::node_info&;
    using pointer = tx_group::node_info*;
    using iterator_category = std::forward_iterator_tag;

    iterator () = default;
    iterator (tx_group::node_info *ptr);
    iterator (const iterator &);
    ~iterator () = default;

    iterator &operator= (const iterator &);
    bool operator== (const iterator &) const;
    bool operator!= (const iterator &) const;

    iterator &operator++ ();
    reference operator*() const;
    pointer operator->() const;

  private:
    tx_group::node_info *m_ptr;
};

class tx_group::const_iterator
{
  public:
    using difference_type = std::ptrdiff_t;
    using value_type = tx_group::node_info;
    using reference = const tx_group::node_info&;
    using pointer = const tx_group::node_info*;
    using iterator_category = std::forward_iterator_tag;

    const_iterator () = default;
    const_iterator (const tx_group::node_info *ptr);
    const_iterator (const const_iterator &);
    ~const_iterator () = default;

    const_iterator &operator= (const const_iterator &);
    bool operator== (const const_iterator &) const;
    bool operator!= (const const_iterator &) const;

    const_iterator &operator++ ();
    reference operator* () const;
    pointer operator-> () const;

  private:
    const tx_group::node_info *m_ptr;
};

#endif // !_TRANSACTION_GROUP_HPP_
