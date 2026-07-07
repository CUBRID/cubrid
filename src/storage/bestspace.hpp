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
// bestspace.hpp - bestspace in memory
//

#ifndef _BESTSPACE_HPP_
#define _BESTSPACE_HPP_

#include "tbb/concurrent_queue.h"
#include "thread_entry.hpp"
#include "page_buffer.h"
#include "storage_common.h"
#include "dbtype_def.h"

#include <optional>
#include <array>
#include <limits>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <deque>

namespace cubstorage
{
  //////////////////////////////////////////////////////////////////////////
  // export class
  //////////////////////////////////////////////////////////////////////////

  typedef struct bestspace_entry BESTSPACE_ENTRY;
  struct bestspace_entry
  {
    std::uint16_t freespace;
    short volid;
    int32_t pageid;
  };

  static_assert (sizeof (bestspace_entry) == 8, "bestspace_entry must be 8 bytes");
  static_assert (offsetof (bestspace_entry, freespace) == 0, "freespace must be placed at first");
  static_assert (offsetof (bestspace_entry, volid) == 2, "volid must be placed at second");
  static_assert (offsetof (bestspace_entry, pageid) == 4, "pageid must be placed at last");

  //////////////////////////////////////////////////////////////////////////
  // base class
  //////////////////////////////////////////////////////////////////////////

#if defined (UNIT_TEST_BESTSPACE)
  struct bestspace_test_probe;
#endif

  class bestspace
  {
#if defined (UNIT_TEST_BESTSPACE)
      friend struct bestspace_test_probe;
#endif

    public:
      static constexpr std::size_t BITS_PER_BYTE = std::numeric_limits<unsigned char>::digits;
      static constexpr std::size_t ALLOC_BATCH_SIZE = 4;
      static constexpr std::size_t L3_FANOUT = 8;
      static constexpr std::size_t L2_FANOUT = 8;
      static constexpr std::size_t ENTRIES_PER_SHARD = L3_FANOUT * L2_FANOUT;
      static constexpr std::size_t DEFAULT_SHARD_COUNT = 8;

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

	atomic_wrapper ()
	  : value ()
	{
	}

	atomic_wrapper (T val)
	  : value (val)
	{
	}

	T load () const noexcept
	{
	  return value.load ();
	}

	void store (T desired) noexcept
	{
	  value.store (desired);
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

	  void clear ();
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

	  std::size_t find (tier minimum, std::array<std::size_t, BITS_PER_BYTE> &pos);

	  void clear ();
	  void clear (std::size_t index);
	  void set (tier fs, std::size_t index);

	  friend bool operator== (const L3 &lhs, const L3 &rhs)
	  {
	    return std::memcmp (lhs.m_freespace.data (), rhs.m_freespace.data (), 8) == 0;
	  }

	private:
	  std::array<bitmap, 8> m_freespace;
      };

      class alignas (64) shard
      {
#if defined (UNIT_TEST_BESTSPACE)
	  friend struct bestspace_test_probe;
#endif

	public:
	  shard (bestspace &parent) noexcept;
	  ~shard () = default;

	  void initialize_by_entries (const bestspace_entry entries[ENTRIES_PER_SHARD]);

	  status find (OID *class_oid, HFID *hfid, std::uint16_t needed_size, std::uint16_t consume_size,
		       std::size_t bias, PGBUF_WATCHER &page_watcher);

	  void get_stats (std::uint32_t &request, std::uint32_t &advanced_shard, std::uint32_t &fetch_L3, std::uint32_t &fetch_L2,
			  std::uint32_t &fetch_L1, std::uint32_t &found, std::uint32_t &allocated);

	private:
	  atomic_wrapper<bool> m_allocating;

	  atomic_wrapper<L3> m_L3;
	  atomic_wrapper<L2> m_L2[L3_FANOUT];
	  atomic_wrapper<L1> m_L1[L3_FANOUT * L2_FANOUT];

	  std::atomic<std::uint64_t> m_recs_num;
	  std::atomic<std::uint64_t> m_recs_sumlen;

	  bestspace &m_parent;
	  struct
	  {
	    bool enabled;

	    std::atomic<std::uint32_t> request;
	    std::atomic<std::uint32_t> advance_shard;

	    std::atomic<std::uint32_t> fetch_L3;
	    std::atomic<std::uint32_t> fetch_L2;
	    std::atomic<std::uint32_t> fetch_L1;

	    std::atomic<std::uint32_t> found;
	    std::atomic<std::uint32_t> allocated;
	  } m_stats;

	  status L3_find (OID *class_oid, tier minimum, std::uint16_t needed_size, std::uint16_t consume_size,
			  std::size_t bias, PGBUF_WATCHER &page_watcher);
	  void L3_update (std::size_t l2_index);

	  status L2_find (OID *class_oid, tier minimum, std::uint16_t needed_size, std::uint16_t consume_size,
			  std::size_t l2_index, std::size_t bias, PGBUF_WATCHER &page_watcher);
	  void L2_update (std::size_t l2_index, std::size_t l1_index);

	  status L1_find (OID *class_oid, std::uint16_t needed_size, std::uint16_t consume_size, std::size_t l2_index,
			  std::size_t l1_index, PGBUF_WATCHER &page_watcher);
	  status L1_fix (std::size_t l2_index, std::size_t l1_index, L1 l1, VPID vpid, PGBUF_WATCHER &page_watcher);
	  void L1_remove (std::size_t l2_index, std::size_t l1_index, L1 l1);

	  status allocate_mark ();
	  void allocate_unmark ();

	  void allocate_pick_victims (std::array<VPID, L3_FANOUT * L2_FANOUT + ALLOC_BATCH_SIZE> &residents,
				      std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims);
	  std::size_t allocate_pick_candidates (std::array<VPID, L3_FANOUT * L2_FANOUT + ALLOC_BATCH_SIZE> &residents,
						std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims,
						std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates);

	  int allocate_new_pages (HFID *hfid, std::size_t num_candidates,
				  std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates, PGBUF_WATCHER &page_watcher);
	  void allocate_replace_pages (std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims,
				       std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates);

	  status allocate (HFID *hfid, std::uint16_t consume_size, PGBUF_WATCHER &page_watcher);
      };

    public:
      explicit bestspace (std::uint16_t unfill_space = 0, std::size_t shard_count = DEFAULT_SHARD_COUNT);
      ~bestspace () = default;

      void initialize_by_entries (const bestspace_entry *entries, std::size_t num_entries);

      void add_candidates (bestspace_entry *candidates, std::size_t num_candidates);
      bool pop_candidate (bestspace_entry &candidate);

      int find (cubthread::entry &thread_ref, OID *class_oid, HFID *hfid, std::uint16_t size, PGBUF_WATCHER &page_watcher);

      static tier size_to_tier (std::uint16_t size);

      void show_stats ();

    private:
      std::deque<shard> m_shards;
      tbb::concurrent_queue<bestspace_entry> m_candidates;

      // parameter
      std::uint16_t m_unfill_space;

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

      static_assert (sizeof (shard) == 4800, "bestspace::shard must be 4800 bytes");
      static_assert (alignof (shard) == 64, "bestspace::shard must be aligned as 64 bytes");
  };

  //////////////////////////////////////////////////////////////////////////
  // bestspace register/unregister
  //////////////////////////////////////////////////////////////////////////

  class bestspace_registry
  {
    private:
      struct registry_entry
      {
	OID class_oid;
	HFID hfid;
	bestspace *entry;

	registry_entry *next;
      };

      struct registry_cache
      {
	registry_entry *head;
	std::size_t size;
	std::size_t generation;

	registry_cache ();
	~registry_cache ();
      };

    public:
      bestspace_registry ();
      ~bestspace_registry ();

      void create (OID *class_oid, HFID *hfid, bestspace_entry *entries, std::size_t num_entries,
		   bestspace_entry *candidates, std::size_t num_candidates, std::size_t shard_count = bestspace::DEFAULT_SHARD_COUNT,
		   std::uint16_t unfill_space = 0);
      void destroy (OID *class_oid, HFID *hfid);

      bestspace *find (OID *class_oid, HFID *hfid);

      void show_stats ();

    private:
      registry_entry *m_head;
      std::mutex m_mutex;

      alignas (64) std::atomic<uint64_t> m_generation;

      static constexpr std::size_t TLS_MAX_SIZE = 20;
      inline static thread_local registry_cache TLS_cache;

      bestspace *find_from_cache (OID *class_oid, HFID *hfid);
      bestspace *find_from_global (OID *class_oid, HFID *hfid);

      void insert_entry (registry_entry *&head, registry_entry *entry);
      void destroy_entry (registry_entry *entry);
      std::optional<std::pair<registry_entry *, registry_entry *>> find_entry (registry_entry *head,
	  const OID *class_oid, const VFID *vfid);
      std::optional<std::pair<registry_entry *, registry_entry *>> find_entry (registry_entry *head,
	  const OID *class_oid, const HFID *hfid);

      void invalidate_entries (registry_entry *head);

      registry_entry *get_node_from_list (registry_entry *&head, const OID *class_oid, const VFID *vfid);
      registry_entry *get_node_from_list (registry_entry *&head, const OID *class_oid, const HFID *hfid);
      registry_entry *get_tail_from_list (registry_entry *&head);
  };

  extern bestspace_registry bestspaces;
}

#endif // _BESTSPACE_HPP_
