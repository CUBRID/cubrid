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
// MVCC active transactions map
//

#ifndef _MVCC_ACTIVE_TRAN_HPP_
#define _MVCC_ACTIVE_TRAN_HPP_

#include "storage_common.h"

#include <cassert>
#include <cstdint>

struct mvcc_active_tran
{
  public:
    mvcc_active_tran ();
    ~mvcc_active_tran ();

    enum class copy_safety
    {
      THREAD_SAFE,
      THREAD_UNSAFE
    };

    void initialize ();
    void finalize ();
    void reset ();

    MVCCID get_bit_area_start_mvccid ();

    bool is_active (MVCCID mvccid) const;
    void copy_to (mvcc_active_tran &dest, copy_safety safety) const;
    mvcc_active_tran &operator= (const mvcc_active_tran &other) = delete;

    MVCCID compute_highest_completed_mvccid () const;
    MVCCID compute_lowest_active_mvccid () const;

    void set_inactive_mvccid (MVCCID mvccid);
    void reset_start_mvccid (MVCCID mvccid);

    void reset_active_transactions ();
    void check_valid () const;

  private:
    using unit_type = std::uint64_t;

    static const size_t BITAREA_MAX_SIZE = 500;

    static const size_t BYTE_BIT_COUNT = 8;
    static const size_t UNIT_BYTE_COUNT = sizeof (unit_type);
    static const size_t UNIT_BIT_COUNT = UNIT_BYTE_COUNT * BYTE_BIT_COUNT;

    static const size_t BITAREA_MAX_MEMSIZE = BITAREA_MAX_SIZE * UNIT_BYTE_COUNT;
    static const size_t BITAREA_MAX_BITS = BITAREA_MAX_SIZE * UNIT_BIT_COUNT;

    static const unit_type ALL_ACTIVE = 0;
    static const unit_type ALL_COMMITTED = (unit_type) -1;

    /* bit area to store MVCCIDS status - size BITAREA_MAX_SIZE */
    unit_type *m_bit_area;
    /* first MVCCID whose status is stored in bit area */
    volatile MVCCID m_bit_area_start_mvccid;
    /* the area length expressed in bits */
    volatile size_t m_bit_area_length;

    /* long time transaction mvccid array */
    MVCCID *m_long_tran_mvccids;
    /* long time transactions mvccid array length */
    volatile size_t m_long_tran_mvccids_length;

    bool m_initialized;

    inline static size_t long_tran_max_size ();

    inline static size_t bit_size_to_unit_size (size_t bit_count);

    inline static size_t units_to_bits (size_t unit_count);
    inline static size_t units_to_bytes (size_t unit_count);

    inline static unit_type get_mask_of (size_t bit_offset);

    inline size_t get_bit_offset (MVCCID mvccid) const;
    inline MVCCID get_mvccid (size_t bit_offset) const;
    inline unit_type *get_unit_of (size_t bit_offset) const;
    inline bool is_set (size_t bit_offset) const;

    size_t get_area_size () const;
    size_t get_bit_area_memsize () const;
    size_t get_long_tran_memsize () const;

    void remove_long_transaction (MVCCID mvccid);
    void add_long_transaction (MVCCID mvccid);
    void ltrim_area (size_t trim_size);
    void set_bitarea_mvccid (MVCCID mvccid);
    void cleanup_migrate_to_long_transations ();
};

#endif // !_MVCC_ACTIVE_TRAN_HPP_
