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
// bestspace_map.hpp - free space tier of every page of a heap file
//
// One record per disk sector (64 pages), numbered in the order the map first sees each sector. The first
// INLINE_SECTORS records live in the map object; a third sector adds a hint tree (root -> mid -> leafdir -> group,
// 64 children each) and a radix index from sector key to number. Bit i of a node's word for a tier means "child i
// may hold a claimable page of this tier". Writers only set bits, bottom-up; a claim clears a bit only after it found
// nothing below, and a bit it puts back sets the bits above again (set_above ()), because a claim one level up may
// have cleared them meanwhile. So a bit can be stale-on but never stale-off. Records and nodes are freed only with
// the map, so lock-free readers never see freed memory. Adding a sector takes a per-map mutex; the rest is atomic.
//

#ifndef _BESTSPACE_MAP_HPP_
#define _BESTSPACE_MAP_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace cubstorage
{
  // the persisted image of one sector (40 bytes in a map page or in the heap header)
  struct bestspace_map_entry
  {
    std::uint64_t key;		// ((volid + 1) << 32) | sectid; 0 is an empty slot
    std::uint64_t codes[4];	// page i of the sector: bits (i % 16) * 4 of codes[i / 16]
  };
  static_assert (sizeof (bestspace_map_entry) == 40, "bestspace_map_entry is persisted as 40 bytes");

  // a page handed out by claim () or next_unverified ()
  struct bestspace_map_page
  {
    short volid;
    std::int32_t pageid;
    std::uint8_t code;
  };

  class bestspace_map
  {
    public:
      static constexpr std::size_t PAGES_PER_SECTOR = 64;
      static constexpr std::size_t FANOUT = 64;
      static constexpr std::size_t NUM_TIERS = 8;
      static constexpr std::size_t INLINE_SECTORS = 2;
      static constexpr std::size_t MAX_SECTORS = FANOUT * FANOUT * FANOUT * FANOUT;
      // a claim gives up after this many sectors that had nothing to hand out (stale hint bits)
      static constexpr std::size_t CLAIM_VISIT_LIMIT = 64;

      static constexpr std::uint8_t CODE_NONE = 0;		// free space <= 7%
      static constexpr std::uint8_t CODE_MAX_TIER = 8;		// codes 1..8 are the tiers FS1..FS8
      static constexpr std::uint8_t CODE_NOT_DATA = 0xE;	// map page, removed page, page not allocated
      static constexpr std::uint8_t CODE_UNVERIFIED = 0xF;	// the map does not know the page

      enum counter
      {
	CNT_FEEDBACK,
	CNT_CLAIMED,
	CNT_VERIFY_FAIL,
	CNT_RELEASED,
	CNT_ALLOC_BATCH,
	CNT_REUSED,
	CNT_NODE_RESERVED,	// nodes allocated by reserve ()
	CNT_NODE_FALLBACK,	// nodes set_resident () allocated because the reserve did not hold them
	// claimed pages a refill's first pass skipped (not buffered, or latched); unchecked = DEFERRED - DISK_VERIFY
	CNT_DEFERRED,
	CNT_DISK_VERIFY,	// claimed pages a refill checked with a fix that may read the disk (one per refill)
	CNT_VICTIM_MEASURED,	// evicted pages measured in the buffer and written back with release_latched ()
	CNT_LAST
      };
      // names for SHOW HEAP CAPACITY, in enum order (checked below the class)
      static constexpr const char *COUNTER_NAMES[] =
      {
	"feedback",
	"claimed",
	"verify_fail",
	"released",
	"alloc_batch",
	"reused",
	"node_reserved",
	"node_fallback",
	"deferred",
	"disk_verify",
	"victim_measured"
      };

      bestspace_map ();
      ~bestspace_map ();
      bestspace_map (const bestspace_map &) = delete;
      bestspace_map &operator= (const bestspace_map &) = delete;

      // heap allocation without exceptions: nullptr when out of memory
      static bestspace_map *create ();
      static void destroy (bestspace_map *&map);

      // ---- updates. false: the sector could not be added (out of memory or MAX_SECTORS); needs_retry () is set.
      // free space measured under the page latch (vacuum, delete, rollback, statistics, recovery)
      bool feedback (short volid, std::int32_t pageid, std::uint8_t code);
      // feedback for a page whose sector the map already has; false (nothing changed) for an unknown sector
      bool feedback_if_known (short volid, std::int32_t pageid, std::uint8_t code);
      // a page enters a shard L1 slot. A new sector's other pages are NOT_DATA, or UNVERIFIED while needs_retry () is
      // set (the map may have missed the sector)
      bool set_resident (short volid, std::int32_t pageid, std::uint8_t code);
      // a page leaves its L1 slot with the caller's code (measured: read under a fix), unless a code was fed back while
      // it was resident or is being released: that one wins
      void release (short volid, std::int32_t pageid, std::uint8_t code, bool measured);
      // a page leaves its L1 slot with a code measured under the page latch the caller still holds. It wins over a
      // fed-back code (feedback needs that latch too); the page ends neither resident nor dirty
      void release_latched (short volid, std::int32_t pageid, std::uint8_t code);
      // the page left the heap
      void forget (short volid, std::int32_t pageid);
      // the daemon found the page gone without its latch: NOT_DATA (true) only if still UNVERIFIED and not resident
      bool forget_unverified (short volid, std::int32_t pageid);
      // write a slot's code for its resident, clean page so the checkpoint image carries it (heap_file.c); no-op
      // otherwise or if a code is not a tier. Marks the sector persist-dirty. true: the code changed
      bool set_resident_code (short volid, std::int32_t pageid, std::uint8_t code);
      // allocate, before the heap header latch, the nodes set_resident () needs if the next sector follows the last one
      // on the same volume. Best effort: set_resident () allocates what is missing, and the promotion, under the latch
      void reserve ();

      // ---- search
      // up to max_count non-resident pages with min_code <= code <= CODE_MAX_TIER; they become resident
      std::size_t claim (std::uint8_t min_code, bestspace_map_page *out, std::size_t max_count);
      bool peek (short volid, std::int32_t pageid, std::uint8_t &code, bool &resident);
      // bit i: pages[i] is resident and was fed back since, with a code above pages[i].code (count <= 64)
      std::uint64_t resident_dirty_above (const bestspace_map_page *pages, std::size_t count);

      // ---- persistence
      std::size_t num_sectors () const;
      std::size_t num_groups () const;	// persist-dirty words: group g covers sectors [g * 64, g * 64 + 64)
      std::uint64_t take_persist_dirty (std::size_t group);
      void put_back_persist_dirty (std::size_t group, std::uint64_t bits);
      bool any_persist_dirty ();
      void snapshot_sector (std::size_t number, bestspace_map_entry &out);
      // load: append a persisted sector as the next number, not dirty. false: duplicate key or out of memory
      bool load_sector (const bestspace_map_entry &entry);
      // apply recorded pages (mask) of one sector like feedback; adds the sector (other pages UNVERIFIED) if new
      bool merge_sector (const bestspace_map_entry &entry, std::uint64_t mask);
      // a file sector the map lacks: allocated pages UNVERIFIED, others NOT_DATA; no-op if known (or added meanwhile)
      bool add_missing_sector (short volid, std::int32_t first_pageid, std::uint64_t allocated);
      // non-resident UNVERIFIED pages from cursor (page index over all sectors); returns how many were written
      std::size_t next_unverified (std::size_t &cursor, bestspace_map_page *out, std::size_t max_count);
      // every page that is not NOT_DATA becomes UNVERIFIED (feedback for the heap was lost); resident pages too
      void mark_all_unverified ();

      // ---- state
      bool is_inline () const;
      std::uint64_t num_unverified () const;
      bool needs_retry () const;
      // the size of the last node allocation that failed while adding a sector (0: none), for an out-of-memory message
      std::size_t failed_alloc_bytes () const;
      // a check the map needs could not run now (the maintenance daemon runs it again)
      void mark_needs_retry ();
      void clear_needs_retry ();
      // a global count bumped when a map may have new work for the maintenance daemon (a page became UNVERIFIED, a
      // sector was added, needs_retry was set). The daemon skips a round while it is unchanged (heap_file.c)
      static std::uint64_t work_epoch ();
      static void note_work ();
      // counts[c]: pages with code c over every sector; atomic loads, not a snapshot
      void count_codes (std::uint64_t counts[16]);
      void count (counter id, std::uint64_t value = 1);
      void get_usage (std::size_t &bytes, std::uint64_t counters[CNT_LAST]);

      static std::uint64_t make_key (short volid, std::int32_t pageid);
      static short key_volid (std::uint64_t key);
      static std::int32_t key_first_pageid (std::uint64_t key);
      static std::uint8_t entry_code (const bestspace_map_entry &entry, std::size_t offset);
      static void entry_set_code (bestspace_map_entry &entry, std::size_t offset, std::uint8_t code);

#if !defined (NDEBUG)
      // the next count internal allocations fail (unit tests)
      static void debug_fail_allocations (int count);
      // set_resident_code () calls hook (arg) just before its exchange (unit tests). nullptr: none
      static void debug_resident_code_hook (void (*hook) (void *), void *arg);
#endif

    private:
      struct hint
      {
	std::atomic<std::uint64_t> bits[NUM_TIERS];
      };

      struct record
      {
	std::atomic<std::uint64_t> codes[4];	// 64 x 4-bit page codes
	std::atomic<std::uint64_t> resident;	// page is held by a shard L1 slot
	std::atomic<std::uint64_t> dirty;	// free space was fed back while resident
	std::atomic<std::uint64_t> version;	// bumped by every published change
      };

      struct group
      {
	hint h;				// bit s: sector s of this group
	std::atomic<std::uint64_t> persist_dirty;	// sectors changed since the last write (sectors >= INLINE)
	std::uint64_t keys[FANOUT];	// immutable once the sector is published
	record sectors[FANOUT];		// group 0: slots < INLINE_SECTORS unused (those live in m_inline)
      };

      template <typename Child>
      struct node
      {
	hint h;
	std::atomic<Child *> children[FANOUT];
      };

      using leafdir = node<group>;
      using middir = node<leafdir>;
      using rootdir = node<middir>;

      // radix index: 40-bit key (15-bit volid, 25-bit sectid), 7 levels of 6 bits
      static constexpr int RADIX_LEVELS = 7;
      struct rnode
      {
	std::atomic<void *> slots[FANOUT];	// inner: child rnode *, leaf: sector number + 1
      };

      record m_inline[INLINE_SECTORS];
      std::uint64_t m_inline_keys[INLINE_SECTORS];
      std::atomic<std::uint64_t> m_inline_persist_dirty;
      std::atomic<rootdir *> m_root;	// null while inline
      std::atomic<rnode *> m_radix;	// null while inline
      std::atomic<std::size_t> m_num_sectors;
      std::atomic<std::uint64_t> m_num_unverified;
      std::atomic<bool> m_needs_retry;
      std::mutex m_grow_mutex;
      std::atomic<std::size_t> m_bytes;	// written under m_grow_mutex, read without it (get_usage ())
      std::atomic<std::size_t> m_failed_alloc_bytes;
      std::atomic<std::uint64_t> m_counters[CNT_LAST];

      // reserve ()'s nodes for the next sector, under m_grow_mutex: a group, its directories and its key's radix path
      static constexpr std::size_t MAX_SPARE_RNODES = RADIX_LEVELS - 1;
      struct spare_nodes
      {
	middir *mid;
	leafdir *leaf;
	group *grp;
	rnode *rnodes[MAX_SPARE_RNODES];
	std::size_t num_rnodes;
      };
      struct node_counts
      {
	std::size_t mid;
	std::size_t leaf;
	std::size_t grp;
	std::size_t rnodes;
      };
      spare_nodes m_spare;
      std::atomic<std::size_t> m_reserved_for;	// the sector number the reserve was filled for

      static std::uint64_t radix_key (std::uint64_t key);
      static std::size_t radix_slot (std::uint64_t rk, int level);
      static std::uint8_t get_code (record *rec, std::size_t off);
      static void init_record (record &rec, const std::uint64_t fills[4]);
      static std::uint64_t mask_of (hint &h, std::uint8_t min_code);
      static std::uint32_t prune (hint &parent, hint &child, std::size_t i, std::uint8_t min_code);
      static void set_bit (std::atomic<std::uint64_t> &w, std::uint64_t bit);

      group *group_ptr (std::size_t g);	// null if not allocated
      group *group_at (std::size_t g);	// must exist
      record *sector_at (std::size_t n);
      std::uint64_t key_at (std::size_t n);
      std::atomic<std::uint64_t> &persist_word (std::size_t n);

      record *find (short volid, std::int32_t pageid, std::size_t &n);
      // use_spares: take a new sector's nodes from the reserve (set_resident ())
      record *get_or_add (short volid, std::int32_t pageid, std::size_t &n, std::uint64_t fill, bool use_spares);
      // fills: the codes of a new sector, written before it is published. added: this call added it
      record *get_or_add (short volid, std::int32_t pageid, std::size_t &n, const std::uint64_t fills[4],
			  bool use_spares, bool &added);
      bool promote (bool use_spares);
      bool add_group (rootdir *root, std::size_t g, bool use_spares);
      bool radix_insert (std::atomic<rnode *> &root, std::uint64_t rk, std::size_t number, bool use_spares);
      void free_radix (rnode *r, int level);
      void free_tree (rootdir *root);
      template <typename T>
      T *alloc_node (T *&spare, bool use_spares);
      rnode *alloc_rnode (bool use_spares);
      void count_needs (std::size_t s, node_counts &need);

      void set_code (record *rec, std::size_t n, std::size_t off, std::uint8_t code);
      void write_feedback (record *rec, std::size_t n, std::int32_t pageid, std::uint8_t code);
      void count_unverified_change (std::uint8_t old_code, std::uint8_t new_code);
      void publish (record *rec, std::size_t n, std::size_t off, bool mark_dirty);
      void set_above (std::size_t depth, std::size_t index, std::uint32_t tiers);
      bool claim_sector (record *rec, std::uint64_t key, std::uint8_t min_code, bestspace_map_page *out,
			 std::size_t max_count, std::size_t &found);

      template <typename Node, typename Child>
      void claim_node (Node &nd, std::size_t base, std::uint8_t min_code, bestspace_map_page *out,
		       std::size_t max_count, std::size_t &found, std::size_t &budget);
      void descend (middir &m, std::size_t base, std::uint8_t min_code, bestspace_map_page *out, std::size_t max_count,
		    std::size_t &found, std::size_t &budget);
      void descend (leafdir &l, std::size_t base, std::uint8_t min_code, bestspace_map_page *out, std::size_t max_count,
		    std::size_t &found, std::size_t &budget);
      void descend (group &grp, std::size_t g, std::uint8_t min_code, bestspace_map_page *out, std::size_t max_count,
		    std::size_t &found, std::size_t &budget);
  };
  static_assert (sizeof (bestspace_map::COUNTER_NAMES) / sizeof (bestspace_map::COUNTER_NAMES[0])
		 == bestspace_map::CNT_LAST, "COUNTER_NAMES needs one name per counter, in enum order");

  // tiers for heaps whose map is not in memory, one entry per (group, sector key) with a mask of the recorded pages.
  //   pending table (runtime, feedback before the map is loaded): group = heap file key (heap_file.c), owner and lsa 0.
  //   recovery table (restart): group = 0, owner = class OID; a sector follows the owner with the largest LSA,
  //   because a sector moves to another file only after its file was destroyed.
  class bestspace_sector_table
  {
    public:
      struct entry
      {
	std::uint64_t group;
	std::uint64_t key;
	std::uint64_t owner;
	std::uint64_t owner_lsa;
	std::uint64_t mask;
	std::uint64_t codes[4];
	entry *next;
      };

      static constexpr std::size_t MAX_LOST = 256;

      enum record_result
      {
	RECORDED,
	NO_MEMORY,
	REJECTED
      };

      bestspace_sector_table ();
      ~bestspace_sector_table ();
      bestspace_sector_table (const bestspace_sector_table &) = delete;
      bestspace_sector_table &operator= (const bestspace_sector_table &) = delete;

      // false: no memory for the entry; the caller calls mark_lost ()
      bool record (std::uint64_t group, std::uint64_t key, std::size_t offset, std::uint8_t code, std::uint64_t owner,
		   std::uint64_t lsa);
      // like record () with owner and lsa 0, unless check (arg) fails under the stripe lock (the pending table checks
      // the map is still not loaded; a loader publishes the map, then takes the group under that lock). NO_MEMORY:
      // (group, 0) is marked lost under the lock, so the loader sees the mark
      record_result record_if (std::uint64_t group, std::uint64_t key, std::size_t offset, std::uint8_t code,
			       bool (*check) (void *), void *arg);
      // the recorded pages (mask) of one sector at once, owner and lsa 0 (pending table)
      bool record_sector (std::uint64_t group, std::uint64_t key, const std::uint64_t codes[4], std::uint64_t mask);
      // put taken pages of one sector back (pending table), except pages recorded again since the take (they have a
      // newer code). false: no memory
      bool restore_sector (std::uint64_t group, std::uint64_t key, const std::uint64_t codes[4], std::uint64_t mask);
      // the groups (!= 0) with an entry or a lost mark (owner 0), sorted and distinct; a group recorded meanwhile may
      // be missing. groups is malloc'd (free_and_init), nullptr when count is 0. false: no memory
      bool list_groups (std::uint64_t *&groups, std::size_t &count);
      // the group (!= 0) has an entry or a lost mark (owner 0) now
      bool has_group (std::uint64_t group);
      // detach every entry of a group (!= 0); free with free_list (). locked (list, arg) runs under the stripe lock,
      // ordered against every record_if () of the group; it must not call back into this table
      entry *take_group (std::uint64_t group, void (*locked) (entry *list, void *arg) = nullptr, void *arg = nullptr);
      entry *take_all ();
      // back to the state as constructed, lost marks included. Not while other threads use the table
      void clear ();
      static void free_list (entry *&list);
      std::size_t size () const;

      // a record () failed: remember (group, owner) so the caller can verify that heap whole
      void mark_lost (std::uint64_t group, std::uint64_t owner);
      bool is_lost (std::uint64_t group, std::uint64_t owner);
      bool lost_all () const;
      void clear_lost (std::uint64_t group, std::uint64_t owner);
      // remove one remembered (group, owner); false when none is left
      bool take_lost (std::uint64_t &group, std::uint64_t &owner);

    private:
      static constexpr std::size_t STRIPES = 64;
      static constexpr std::size_t INITIAL_BUCKETS = 64;

      struct stripe
      {
	std::mutex mutex;
	entry **buckets;
	std::size_t num_buckets;
	std::size_t count;
      };

      stripe m_stripes[STRIPES];
      std::atomic<std::size_t> m_size;
      std::mutex m_lost_mutex;
      std::uint64_t m_lost[MAX_LOST][2];
      std::size_t m_num_lost;
      std::atomic<bool> m_lost_all;

      static std::uint64_t mix (std::uint64_t v);
      static std::size_t stripe_of (std::uint64_t group, std::uint64_t key);
      static std::size_t bucket_of (const stripe &s, std::uint64_t group, std::uint64_t key);
      void grow (stripe &s);
      bool record_locked (stripe &s, std::uint64_t group, std::uint64_t key, std::size_t offset, std::uint8_t code,
			  std::uint64_t owner, std::uint64_t lsa);
      entry *detach_if (stripe &s, std::uint64_t group, bool all);
      static bool grow_group_list (std::uint64_t *&list, std::size_t &cap, std::size_t need);
  };
}

#endif // _BESTSPACE_MAP_HPP_
