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
#include "bestspace_map.hpp"

#include <optional>
#include <array>
#include <limits>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <condition_variable>
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
	FAILURE,
	DEFERRED	// a claimed page not checked: not in the page buffer, or latched by another thread (05 7.2)
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

	T exchange (T desired) noexcept
	{
	  return value.exchange (desired);
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
	  // the room for a new record without saved space (spage_max_space_for_new_record_without_saving ()), less the
	  // records given since it was measured: what L1_find ()'s pre-check and the victim choice compare with a request.
	  // A slot whose page has not been fixed yet (a claimed page left unverified, a page of a new batch) holds the
	  // upper bound of the page's tier instead (optimistic_freespace ()), which the first fix of the page corrects.
	  // Either value reaches the map only through code_of_room ()
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

	  status find (OID *class_oid, HFID *hfid, std::uint16_t needed_size, std::uint16_t consume_size,
		       std::size_t bias, PGBUF_WATCHER &page_watcher);

	  void add_estimates (int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen);
	  void subtract_estimates (int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen);
	  void get_estimates (int &num_pages, std::uint64_t &recs_num, std::uint64_t &recs_sumlen);
	  void get_stats (std::uint32_t &request, std::uint32_t &advanced_shard, std::uint32_t &fetch_L3, std::uint32_t &fetch_L2,
			  std::uint32_t &fetch_L1, std::uint32_t &found, std::uint32_t &allocated);
	  // empty every slot that holds the page, without a release (see bestspace::drop_from_slots ()). true: one did
	  bool drop_page (VPID vpid);
	  // write the code each slot keeps for its page into the map (bestspace::publish_slot_codes ()). false: an
	  // allocation held the shard, which was left as it was
	  bool publish_slot_codes (std::size_t &changed);

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
			  std::size_t l1_index, PGBUF_WATCHER &page_watcher);
	  status L1_fix (std::size_t l2_index, std::size_t l1_index, L1 l1, VPID vpid, PGBUF_WATCHER &page_watcher);
	  void L1_remove (std::size_t l2_index, std::size_t l1_index, L1 l1);

	  status allocate_mark ();
	  void allocate_unmark ();

	  std::size_t allocate_pick_victims (std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims,
					     std::uint16_t needed_size);
	  status allocate_release_victims (OID *class_oid, HFID *hfid, std::size_t count,
					   std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE>
					   &victims, PGBUF_WATCHER &page_watcher);

	  // fix a page only if it is in the buffer and no other thread latches it: no I/O, no wait. SUCCESS: fixed,
	  // with the watcher an ordered fix gives it; DEFERRED: not fixed; NOT_FOUND: a bad page id (error cleared);
	  // FAILURE: an error is set
	  static status fix_in_buffer (HFID *hfid, VPID &vpid, PGBUF_WATCHER &page_watcher);

	  // in_buffer_only: no I/O and no wait; DEFERRED when the page cannot be fixed that way
	  status allocate_verify_actual_space (OID *class_oid, HFID *hfid, std::uint16_t needed_size,
					       bestspace_entry &candidate, bool &valid, bool in_buffer_only,
					       PGBUF_WATCHER &page_watcher);

	  int allocate_new_pages (HFID *hfid, std::size_t num_candidates,
				  std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates, PGBUF_WATCHER &page_watcher);
	  void allocate_install (std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims,
				 std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates, std::size_t num_candidates);

	  status allocate (OID *class_oid, HFID *hfid, std::uint16_t needed_size, std::uint16_t consume_size,
			   PGBUF_WATCHER &page_watcher);
      };

    public:
      // map_pages: malloc'd array of map_num_pages map pages (or nullptr); this object takes it over with the map
      explicit bestspace (std::size_t shard_count, int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen,
			  std::uint16_t unfill_space, bestspace_map *map, bool map_inline, VPID *map_pages,
			  std::size_t map_num_pages);
      ~bestspace ();

      int find (cubthread::entry &thread_ref, OID *class_oid, HFID *hfid, std::uint16_t size, bool is_newrec,
		PGBUF_WATCHER &page_watcher);

      static tier size_to_tier (std::uint16_t size);
      // the bestspace_map code of a free space: CODE_NONE for FS0, 1..8 for FS1..FS8
      static std::uint8_t code_of (std::uint16_t freespace);
      // the map code of a slot's room for a new record: the tier of the most free space the room can stand for
      static std::uint8_t code_of_room (std::uint16_t room);
      // the free space a map code promises: the upper bound of its tier, at most a non-header page's capacity
      static std::uint16_t optimistic_freespace (std::uint8_t code);

      bestspace_map *get_map ();

      // empty the shard slots that hold a page that leaves or left the heap, without a release: what a slot records is
      // the free space of the page's life before, and two slots holding the page once it is handed out again would
      // each release it to the map (heap_file.c has the sequence). Called by vacuum's removal of the page, with the
      // page latched (heap_remove_page_on_vacuum ()), and by the allocation that gets the page back, with the heap
      // header latched (heap_alloc_new_pages ()). true: a slot held the page
      bool drop_from_slots (VPID vpid);

      // write what the slots know into the map before a checkpoint writes it (heap_bestspace_checkpoint ()): the code
      // of each slot's room, for a page that is resident and not dirty (bestspace_map::set_resident_code ()). A page
      // keeps the code it had when it became resident (the capacity tier for a new page, the claimed tier) while a
      // slot fills it, and slots are not restored after a restart: without this the image on disk would hand out
      // such a page at that code after the restart, and its first claim would find it full. Returns how many codes
      // changed; a shard an allocation holds at that moment is skipped, and the next checkpoint writes its slots
      std::size_t publish_slot_codes ();

      // the loader has merged the heap's pending feedback (bestspace_pending) into the map. Until then feedback keeps
      // going to the pending table, even though the bestspace is published: fed to the map directly, it could be
      // overwritten by an older code of the same page that the loader merges afterwards (heap_file.c)
      bool is_absorbed () const;
      void set_absorbed ();	// under the pending table's stripe lock of the heap's group

      // where the map is on disk (05 5.1): inline in the heap header, or map pages. heap_file.c sets it at load and when
      // it adds a map page (under the heap header write latch); the checkpoint reads it. m_layout_mutex is taken after
      // page latches, never before one, and no page is fixed while it is held.
      std::size_t map_capacity_sectors (std::size_t per_page);	// inline: INLINE_SECTORS, else pages * per_page
      bool map_page_at (std::size_t page_index, VPID &vpid, bool &is_inline);	// false: no such page (or inline)
      bool map_is_inline ();
      std::size_t map_num_pages ();
      // room for one more map page, so that map_append_page () cannot fail once the page is linked. false: out of memory
      bool map_reserve_page_slot ();
      bool map_append_page (VPID vpid);	// leaves the inline layout; false only without a reserved slot and memory

      static constexpr std::size_t ORPHAN_HINTS = 8;

      // a map page of this heap may be an orphan (05 5.1: in the chain, flagged BESTSPACE, not in the map's list): a
      // map page add failed after its page was linked; a map page add left listed pages out of the list (the load had
      // ended before them); a load ended before the last listed page (they stay listed until that add). hint: the first
      // of the orphans, which links to the others (heap_file.c follows the links); a null hint: the chain must be
      // walked. The caller holds the heap header write latched, or is the maintenance daemon
      void set_orphan_suspect (const VPID &hint);
      bool orphan_suspect () const;
      // false: nothing suspected. Otherwise the hints given since the last take, and whether the chain must be walked
      // (a null hint was given, or more than ORPHAN_HINTS hints)
      bool take_orphan_suspect (VPID hints[ORPHAN_HINTS], std::size_t &num_hints, bool &walk);

      // the maintenance daemon's own state (heap_file.c), used by its thread only
      std::size_t m_verify_cursor;	// where next_unverified () goes on
      VPID m_orphan_follow[ORPHAN_HINTS];	// map pages to look at: the hints, then the pages they link to
      std::size_t m_orphan_num_follow;
      std::size_t m_orphan_followed;	// pages looked at since the hints were taken (links may be stale: a bound)
      VPID m_orphan_walk_next;		// the walk: the next chain page to visit (null: no walk)
      VPID m_orphan_walk_prev;		// the chain page visited before it (that page's prev link)

      void set_estimates (int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen);
      void get_estimates (int &num_pages, std::uint64_t &recs_num, std::uint64_t &recs_sumlen);
      void get_stats (std::uint32_t &request, std::uint32_t &advanced_shard, std::uint32_t &fetch_L3, std::uint32_t &fetch_L2,
		      std::uint32_t &fetch_L1, std::uint32_t &found, std::uint32_t &allocated);

      std::size_t get_num_shards ();

    private:
      std::deque<shard> m_shards;
      bestspace_map *m_map;	// owned: freed with this object
      std::atomic<bool> m_absorbed;	// see is_absorbed ()
      std::atomic<bool> m_orphan_suspect;	// see set_orphan_suspect ()

      // the map layout on disk, under m_layout_mutex
      std::mutex m_layout_mutex;
      // the orphan hints given since the maintenance daemon took them, under m_layout_mutex
      VPID m_orphan_hints[ORPHAN_HINTS];
      std::size_t m_orphan_num_hints;
      bool m_orphan_walk;
      bool m_map_inline;
      VPID *m_map_pages;		// malloc'd, m_map_pages_alloc slots; owned
      std::size_t m_map_num_pages;
      std::size_t m_map_pages_alloc;

      // parameter
      const bool m_distributed_insert;
      std::uint16_t m_unfill_space;

      // statistics
      std::atomic<int> m_num_pages;
      std::atomic<std::uint64_t> m_recs_num;
      std::atomic<std::uint64_t> m_recs_sumlen;

      bool map_grow_page_slots ();	// under m_layout_mutex

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
	int pins;		// pinned by the checkpoint (global list nodes only), under m_mutex
	bool dropped;		// destroyed while pinned: the last unpin frees it, under m_mutex
	bool retired;		// quiesce (retire): no begin_work () any more, under m_mutex
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
      // a bestspace held by the checkpoint while it writes its map with no registry mutex held (05 2.2 rule 4): a
      // destroy () meanwhile only marks it dropped, and the last unpin frees it
      struct pinned
      {
	HFID hfid;
	bestspace *entry;
	void *node;
      };

      // background work on a heap, from begin_work () to end_work (): on the worker's stack, listed under m_mutex
      struct work
      {
	VFID vfid;
	work *next;
      };

      bestspace_registry ();
      ~bestspace_registry ();

      // takes over the map and the map page array (malloc'd, map_num_pages pages, or nullptr; the layout read from
      // disk): the bestspace frees them, or this function does when it returns nullptr (out of memory)
      bestspace *create (HFID *hfid, std::size_t shard_count, int num_pages, std::uint64_t recs_num,
			 std::uint64_t recs_sumlen, std::uint16_t unfill_space, bestspace_map *map, bool map_inline,
			 VPID *map_pages, std::size_t map_num_pages);
      void destroy (const VFID *vfid);
      void destroy (const HFID *hfid);

      bestspace *find (HFID *hfid);

      using callback = int (*) (const HFID *hfid, bestspace *entry, void *args);
      int for_each (callback function, void *args);

      // pin every bestspace: out is malloc'd (nullptr and count 0 when there is none); unpin_all () frees it
      int pin_all (pinned *&out, std::size_t &count);
      void unpin_all (pinned *&pins, std::size_t count);
      // pin the bestspace of one heap; false when it is not in the registry
      bool pin (const HFID *hfid, pinned &out);
      void unpin (pinned &pin);
      // the pinned bestspace was destroyed (its heap dropped or reused) after it was pinned
      bool is_dropped (const pinned &pin);
      // background work on a heap (heap_file.c) is about to hold the heap's header write latched and wait for pages of
      // the heap: the maintenance daemon (map pages, chain pages, and the file header when it allocates or frees a map
      // page) and the checkpoint's writer (map pages). Neither holds a lock on the class. A thread that latches the
      // whole file, file header first (file_destroy (), file_apply_tde_algorithm ()), keeps them away before it
      // starts: destroy () and quiesce () wait until the work in progress on the file ends, and from then on
      // begin_work () refuses it. Otherwise the two could wait for each other: page latches have no deadlock
      // detection. The wait can last seconds: a map page allocation may extend a volume.
      // false: the bestspace is dropped or retired, or its file is quiesced (then *quiesced, when given, is true): the
      // caller leaves the heap alone
      bool begin_work (const pinned &pin, work &w, bool *quiesced = nullptr);
      // the same for the checkpoint's writer of a heap that is not loaded (its pending group): false while the file is
      // quiesced
      bool begin_work (const VFID *vfid, work &w);
      void end_work (work &w);
      // the background work may run on the pinned heap: not dropped, not retired, its file not quiesced
      bool may_work (const pinned &pin);
      // keep the background work away from a heap file: until end_quiesce (), begin_work () refuses the file; returns
      // once the work in progress on it has ended. The caller latches no page of the heap (it would wait for work that
      // may wait for that page). retire: the bestspaces of the file in the registry now refuse begin_work () for good,
      // without being freed (vacuum_rv_notify_dropped_file () when it cannot notify vacuum, which may still use them).
      // At most MAX_QUIESCED files are quiesced at a time; one more waits for a slot
      void quiesce (const VFID *vfid, bool retire);
      void end_quiesce (const VFID *vfid);
      bool empty ();
      // how many bestspaces create () has published. A writer that missed a heap in the registry and then reads the
      // same count knows that no bestspace was published since, for that heap or any other (the pending table's
      // check, heap_file.c), without walking the list
      std::uint64_t create_count () const;

    private:
      registry_entry *m_head;
      std::size_t m_cache_count;
      std::mutex m_mutex;
      std::condition_variable m_work_cv;	// end_work () and end_quiesce () wake a destroy () or a quiesce ()

      static constexpr std::size_t MAX_QUIESCED = 16;
      struct quiesced_file
      {
	VFID vfid;
	int count;		// quiesce () calls not ended yet; 0: a free slot
	bool refused;		// begin_work () refused the file meanwhile: end_quiesce () wakes the daemon
      };
      quiesced_file m_quiesced[MAX_QUIESCED];	// under m_mutex
      work *m_work;		// the background work in progress, under m_mutex

      alignas (64) std::atomic<uint64_t> m_generation;
      alignas (64) std::atomic<uint64_t> m_creates;	// bumped under m_mutex once a created entry is in the list

      inline static thread_local registry_cache TLS_cache;

      bestspace *find_from_cache (HFID *hfid);
      bestspace *find_from_global (HFID *hfid);

      void insert_entry (registry_entry *&head, registry_entry *entry);
      void destroy_entry (registry_entry *entry);
      void destroy_or_drop_entry (registry_entry *entry);	// under m_mutex
      void unpin_locked (registry_entry *entry);	// under m_mutex
      // under m_mutex
      quiesced_file *find_quiesced_locked (const VFID *vfid);
      void wait_work_locked (std::unique_lock<std::mutex> &lock, const VFID *vfid);
      std::optional<std::pair<registry_entry *, registry_entry *>> find_entry (registry_entry *head, const VFID *vfid);
      std::optional<std::pair<registry_entry *, registry_entry *>> find_entry (registry_entry *head, const HFID *hfid);

      void invalidate_entries (registry_entry *head);

      registry_entry *get_node_from_list (registry_entry *&head, const VFID *vfid);
      registry_entry *get_node_from_list (registry_entry *&head, const HFID *hfid);
      registry_entry *get_tail_from_list (registry_entry *&head);
  };

  extern bestspace_registry bestspaces;
  // feedback for heaps whose map is not loaded (group = heap file key, heap_file.c); a loader takes its group
  extern bestspace_sector_table bestspace_pending;
  // restart recovery: the final tier of every heap page it redoes, skips or undoes (group 0, owner = class OID key,
  // lsa = page LSA key; heap_bestspace_rv_record)
  extern bestspace_sector_table bestspace_recovery;
}

#endif // _BESTSPACE_HPP_
