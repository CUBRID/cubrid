/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

//
// bestspace.hpp
//

#ifndef _BESTSPACE_HPP_
#define _BESTSPACE_HPP_

#include "thread_entry.hpp"
#include "storage_common.h"
#include "dbtype_def.h"

#include <array>
#include <limits>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <type_traits>

namespace cubstorage
{
  class bestspace
  {
    private:
      enum class status
      {
	NOT_FOUND,
	FOUND,
	CONTENDED,
	ALLOCATING,
	SUCCESS,
	FAILURE
      };

      enum class tier : std::int8_t
      {
	FS0 = -1,   // 1-7%
	FS1 = 0,    // 8-15%
	FS2,	    // 16-24%
	FS3,	    // 25-34%
	FS4,	    // 35-45%
	FS5,	    // 46-57%
	FS6,	    // 58-70%
	FS7,	    // 71-84%
	FS8,	    // 85-100%
	FSEND	    // END
      };

      friend tier &operator++ (tier &v)
      {
	if (v < tier::FSEND)
	  {
	    v = static_cast<tier> (static_cast<std::int8_t> (v) + 1);
	  }
	return v;
      }

      friend tier operator++ (tier &v, int)
      {
	tier result = v;
	++v;
	return result;
      }

      static constexpr std::size_t BITS_PER_BYTE = std::numeric_limits<unsigned char>::digits;
      static constexpr std::size_t L3_FANOUT = 7;
      static constexpr std::size_t L2_FANOUT = 8;
      static constexpr std::size_t SHARD_COUNT = 8;

      class bitmap
      {
	public:
	  bitmap () noexcept;
	  ~bitmap () = default;

	  bool empty ();

	  void set (std::size_t index);
	  void clear (std::size_t index);

	  std::size_t find (std::array<std::size_t, BITS_PER_BYTE> &pos, std::size_t length = BITS_PER_BYTE);

	private:
	  std::uint8_t m_bits;
      };

      template <typename T>
      struct alignas (64) atomic_wrapper
      {
	std::atomic<T> value;

	T load () const noexcept
	{
	  return value.load ();
	}

	bool compare_exchange_strong (T &expected, T desired) noexcept
	{
	  return value.compare_exchange_strong (expected, desired);
	}
      };

      class L1
      {
	public:
	  L1 () noexcept;
	  ~L1 () = default;

	  std::uint16_t get_freespace ();
	  void set_freespace (std::uint16_t size);

	  VPID get_vpid ();
	  void set_vpid (VPID vpid);

	private:
	  std::uint16_t m_freespace;

	  short m_volid;
	  int32_t m_pageid;
      };

      class L2
      {
	public:
	  L2 () noexcept;
	  ~L2 () = default;

	  std::size_t find (tier minimum, std::array<std::size_t, BITS_PER_BYTE> &pos);

	  std::size_t collect (std::array<tier, BITS_PER_BYTE> &tiers);

	  bool empty (tier fs);

	  void clear (std::size_t index);
	  void set (tier fs, std::size_t index);

	  friend bool operator== (const L2 &lhs, const L2 &rhs)
	  {
	    return std::memcmp (lhs.m_freespace.data (), rhs.m_freespace.data (), 8) == 0;
	  }

	private:
	  std::array<bitmap, 8> m_freespace;
      };

      class L3
      {
	public:
	  L3 () noexcept;
	  ~L3 () = default;

	  static constexpr std::uint64_t FLAG_MASK = 0x8080808080808080;
	  static constexpr std::uint64_t FLAG_ALLOCATING = 0x8000000000000000;

	  std::size_t find (tier minimum, std::array<std::size_t, BITS_PER_BYTE> &pos);

	  void clear (std::size_t index);
	  void set (tier fs, std::size_t index);

	  bool is_allocating ();
	  void clear_allocating ();
	  void set_allocating ();

	  friend bool operator== (const L3 &lhs, const L3 &rhs)
	  {
	    uint64_t lhs_value, rhs_value;

	    std::memcpy (&lhs_value, lhs.m_freespace.data (), sizeof (uint64_t));
	    std::memcpy (&rhs_value, rhs.m_freespace.data (), sizeof (uint64_t));

	    return (lhs_value & ~FLAG_MASK) == (rhs_value & ~FLAG_MASK);
	  }

	private:
	  std::array<bitmap, 8> m_freespace;
      };

      class alignas (64) shard
      {
	public:
	  shard () noexcept;
	  ~shard () = default;

	  status find (std::uint16_t size, std::size_t bias, PAGE_PTR &pgptr);

	private:
	  atomic_wrapper<L3> m_L3;
	  atomic_wrapper<L2> m_L2[L3_FANOUT];
	  atomic_wrapper<L1> m_L1[L3_FANOUT * L2_FANOUT];

	  status L3_find (tier minimum, std::uint16_t size, std::size_t bias, bool wait, PAGE_PTR &pgptr);
	  void L3_update (std::size_t l2_index);

	  status L2_find (tier minimum, std::uint16_t size, std::size_t l2_index, std::size_t bias, bool wait, PAGE_PTR &pgptr);
	  void L2_update (std::size_t l2_index, std::size_t l1_index);

	  status L1_find (std::uint16_t size, std::size_t l2_index, std::size_t l1_index, bool wait, PAGE_PTR &pgptr);
	  void L1_remove (std::size_t l2_index, std::size_t l1_index, L1 l1);

	  status allocate_mark ();
	  void allocate_unmark ();
	  void allocate_pages ();
	  status allocate ();
      };

    public:
      bestspace () noexcept;
      ~bestspace () = default;

      int find (cubthread::entry &thread_ref, std::uint16_t size, PAGE_PTR &pgptr);

      static tier size_to_tier (std::uint16_t size);

    private:
      std::array<shard, SHARD_COUNT> m_shard;

      static_assert (sizeof (bitmap) == 1, "bestspace::bitmap must be 1 byte");
      static_assert (std::is_trivially_copyable<bitmap>::value, "bestspace::bitmap must be trivially copyable");

      static_assert (sizeof (L1) == 8, "bestspace::L1 must be 8 bytes");
      static_assert (sizeof (L2) == 8, "bestspace::L2 must be 8 bytes");
      static_assert (sizeof (L3) == 8, "bestspace::L3 must be 8 bytes");
      static_assert (std::atomic<L1>::is_always_lock_free, "std::atomic<bestspace::L1> must be lock-free");
      static_assert (std::atomic<L2>::is_always_lock_free, "std::atomic<bestspace::L2> must be lock-free");
      static_assert (std::atomic<L3>::is_always_lock_free, "std::atomic<bestspace::L3> must be lock-free");
      static_assert (sizeof (atomic_wrapper<L1>) == 64, "bestspace::atomic_wrapper<L1> must be 64 bytes");
      static_assert (sizeof (atomic_wrapper<L2>) == 64, "bestspace::atomic_wrapper<L2> must be 64 bytes");
      static_assert (sizeof (atomic_wrapper<L3>) == 64, "bestspace::atomic_wrapper<L3> must be 64 bytes");
      static_assert (alignof (atomic_wrapper<L1>) == 64, "bestspace::atomic_wrapper<L1> must be aligned as 64 bytes");
      static_assert (alignof (atomic_wrapper<L2>) == 64, "bestspace::atomic_wrapper<L2> must be aligned as 64 bytes");
      static_assert (alignof (atomic_wrapper<L3>) == 64, "bestspace::atomic_wrapper<L3> must be aligned as 64 bytes");

      static_assert (sizeof (shard) == 4096, "bestspace::shard must be 4096 bytes");
      static_assert (alignof (shard) == 64, "bestspace::shard must be aligned as 64 bytes");
  };
}

#endif // _BESTSPACE_HPP_
