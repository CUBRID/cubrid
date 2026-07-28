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

    void set_null ()
    {
      freespace = 0;
      volid = NULL_VOLID;
      pageid = NULL_PAGEID;
    }
  };

  static_assert (sizeof (bestspace_entry) == 8, "bestspace_entry must be 8 bytes");
  static_assert (offsetof (bestspace_entry, freespace) == 0, "freespace must be placed at first");
  static_assert (offsetof (bestspace_entry, volid) == 2, "volid must be placed at second");
  static_assert (offsetof (bestspace_entry, pageid) == 4, "pageid must be placed at last");

  //////////////////////////////////////////////////////////////////////////
  // base class
  //////////////////////////////////////////////////////////////////////////

  class bestspace
  {
    public:
      static constexpr std::size_t BITS_PER_BYTE = std::numeric_limits<unsigned char>::digits;
      static constexpr std::size_t MAX_CANDIDATES_QUEUE_SIZE = 128;
      static constexpr std::size_t MAX_SHARD_PAGE_COUNT = 4;
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
	public:
	  shard (bestspace &parent) noexcept;
	  ~shard () = default;

	  void reset (const bestspace_entry entries[ENTRIES_PER_SHARD]);

	  status find (OID *class_oid, HFID *hfid, std::uint16_t needed_size, std::uint16_t consume_size,
		       std::size_t bias, PGBUF_WATCHER &page_watcher);
	  status find_candidate (OID *class_oid, std::uint16_t needed_size, std::uint16_t consume_size,
				 bestspace_entry &candidate, bool &valid, PGBUF_WATCHER &page_watcher);

	  void add_estimates (int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen);
	  void subtract_estimates (int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen);
	  void get_estimates (int &num_pages, std::uint64_t &recs_num, std::uint64_t &recs_sumlen);
	  void get_stats (std::uint32_t &request, std::uint32_t &advanced_shard, std::uint32_t &fetch_L3, std::uint32_t &fetch_L2,
			  std::uint32_t &fetch_L1, std::uint32_t &found, std::uint32_t &allocated);

	  void to_entries (bestspace_entry *entries);

	private:
	  // core
	  atomic_wrapper<bool> m_allocating;

	  atomic_wrapper<L3> m_L3;
	  atomic_wrapper<L2> m_L2[L3_FANOUT];
	  atomic_wrapper<L1> m_L1[L3_FANOUT * L2_FANOUT];

	  // information per shard
	  bestspace &m_parent;

	  std::atomic<int> m_num_pages;
	  std::atomic<std::uint64_t> m_recs_num;
	  std::atomic<std::uint64_t> m_recs_sumlen;

	  // stats
	  struct
	  {
	    std::atomic<bool> enabled;

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
			  std::size_t l1_index, PGBUF_WATCHER &page_watcher, bool force_check);
	  status L1_fix (std::size_t l2_index, std::size_t l1_index, L1 l1, VPID vpid, PGBUF_WATCHER &page_watcher);
	  void L1_remove (std::size_t l2_index, std::size_t l1_index, L1 l1);

	  status allocate_mark ();
	  void allocate_unmark ();

	  void allocate_pick_victims (std::array<VPID, L3_FANOUT * L2_FANOUT + ALLOC_BATCH_SIZE> &residents,
				      std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims);
	  std::size_t allocate_pick_candidates (std::array<VPID, L3_FANOUT * L2_FANOUT + ALLOC_BATCH_SIZE> &residents,
						std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims,
						std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates,
						std::array<std::pair<bestspace_entry, std::uint16_t>, ALLOC_BATCH_SIZE> &resident_candidates,
						std::size_t &num_resident_candidates, std::uint16_t needed_size);

	  status allocate_get_candidates_or_update_residents (OID *class_oid, std::uint16_t needed_size,
	      std::uint16_t consume_size, std::array<VPID, L3_FANOUT * L2_FANOUT + ALLOC_BATCH_SIZE> &residents,
	      std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims,
	      std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates, std::size_t &num_candidates, PGBUF_WATCHER &page_watcher);

	  status allocate_verify_actual_space (OID *class_oid, std::uint16_t needed_size, bestspace_entry &candidate,
					       bool &valid, PGBUF_WATCHER &page_watcher);
	  status allocate_verify_or_allocate (OID *class_oid, HFID *hfid, std::uint16_t needed_size,
					      std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates, std::size_t &num_candidates, PGBUF_WATCHER &page_watcher);

	  int allocate_new_pages (HFID *hfid, std::size_t num_candidates,
				  std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates, PGBUF_WATCHER &page_watcher);
	  void allocate_replace_pages (std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims,
				       std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates);

	  status allocate (OID *class_oid, HFID *hfid, std::uint16_t needed_size, std::uint16_t consume_size,
			   PGBUF_WATCHER &page_watcher);
      };

      class candidate_queue
      {
	public:
	  candidate_queue ();
	  ~candidate_queue () = default;

	  void reset ();

	  bool try_push (bestspace_entry candidate);
	  void push (bestspace_entry candidate);

	  bool pop (bestspace_entry &candidate, std::uint16_t needed_size);
	  std::size_t pop (bestspace_entry *candidates, std::uint16_t minimum, std::uint16_t needed_size);

	  std::size_t to_entries (bestspace_entry *candidates);

	private:
	  std::array<bestspace_entry, MAX_CANDIDATES_QUEUE_SIZE> m_array;
	  std::size_t m_size;
	  std::atomic<std::uint16_t> m_max_freespace;

	  std::mutex m_mutex;

	  void remove_if_exist (bestspace_entry &candidate);
	  void insert (bestspace_entry &candidate);
      };

    public:
      explicit bestspace (std::size_t shard_count, int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen,
			  std::uint16_t unfill_space);
      ~bestspace () = default;

      void reset (const bestspace_entry *entries, std::size_t num_entries);

      void try_push_candidates (bestspace_entry *candidates, std::size_t num_candidates);
      void push_candidates (bestspace_entry *candidates, std::size_t num_candidates);
      std::size_t pop_candidates (bestspace_entry *candidates, std::uint16_t minimum, std::uint16_t needed_size);

      bool updatable ();

      int find (cubthread::entry &thread_ref, OID *class_oid, HFID *hfid, std::uint16_t size, bool is_newrec,
		PGBUF_WATCHER &page_watcher);

      static tier size_to_tier (std::uint16_t size);

      void set_estimates (int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen);
      void get_estimates (int &num_pages, std::uint64_t &recs_num, std::uint64_t &recs_sumlen);
      void get_stats (std::uint32_t &request, std::uint32_t &advanced_shard, std::uint32_t &fetch_L3, std::uint32_t &fetch_L2,
		      std::uint32_t &fetch_L1, std::uint32_t &found, std::uint32_t &allocated);

      std::size_t get_num_shards ();

      void to_entries (bestspace_entry *entries, bestspace_entry *candidates, std::size_t &num_candidates);

    private:
      std::deque<shard> m_shards;
      candidate_queue m_candidates;

      // parameter
      const bool m_distributed_insert;
      std::uint16_t m_unfill_space;

      // statistics
      std::atomic<int> m_num_pages;
      std::atomic<std::uint64_t> m_recs_num;
      std::atomic<std::uint64_t> m_recs_sumlen;

      std::atomic<std::uint64_t> m_last_updated;

      int find_from_shards (cubthread::entry &thread_ref, OID *class_oid, HFID *hfid, std::size_t shard,
			    std::uint16_t needed_size, std::uint16_t consume_size, std::size_t bias, bool is_newrec, PGBUF_WATCHER &page_watcher);

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

      void create (HFID *hfid, std::size_t shard_count, bestspace_entry *entries, std::size_t num_entries,
		   bestspace_entry *candidates, std::size_t num_candidates, int num_pages, std::uint64_t recs_num,
		   std::uint64_t recs_sumlen, std::uint16_t unfill_space);
      void destroy (const VFID *vfid);
      void destroy (const HFID *hfid);

      bestspace *find (HFID *hfid);

      using callback = int (*) (const HFID *hfid, bestspace *entry, void *args);
      int for_each (callback function, void *args);

    private:
      registry_entry *m_head;
      std::mutex m_mutex;

      alignas (64) std::atomic<uint64_t> m_generation;

      static constexpr std::size_t TLS_MAX_SIZE = 40;
      inline static thread_local registry_cache TLS_cache;

      bestspace *find_from_cache (HFID *hfid);
      bestspace *find_from_global (HFID *hfid);

      void insert_entry (registry_entry *&head, registry_entry *entry);
      void destroy_entry (registry_entry *entry);
      std::optional<std::pair<registry_entry *, registry_entry *>> find_entry (registry_entry *head, const VFID *vfid);
      std::optional<std::pair<registry_entry *, registry_entry *>> find_entry (registry_entry *head, const HFID *hfid);

      void invalidate_entries (registry_entry *head);

      registry_entry *get_node_from_list (registry_entry *&head, const VFID *vfid);
      registry_entry *get_node_from_list (registry_entry *&head, const HFID *hfid);
      registry_entry *get_tail_from_list (registry_entry *&head);
  };

  extern bestspace_registry bestspaces;
}

#endif // _BESTSPACE_HPP_
