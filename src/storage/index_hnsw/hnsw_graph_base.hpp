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

#pragma once

#include "hnsw_algo_common.hpp"
#include "hnsw_utils.hpp"
#include "hnsw_api.hpp"

namespace cubhnsw
{
  using byte_t = std::byte;
  using neighbors_count_t = uint32_t;

  // =====================================================================
  // utility functions
  // =====================================================================
  static std::string dump_oid (const OID &oid)
  {
    return std::string (std::to_string (oid.volid)) + "|" + std::string (std::to_string (oid.pageid)) + "|" + std::string (
		   std::to_string (oid.slotid));
  }

  inline std::string dump_slot (const slot_id_t &oid)
  {
    return dump_oid (oid);
  }

  // =====================================================================
  // graph layout
  // =====================================================================
  /*
   * Design note: span-based, extensible layouts
   *
   * All graph-related structures in this file are span-based views over a
   * contiguous memory region ("tape_").
   *
   * - Each structure defines a fixed-size header.
   * - Variable-sized data (vectors, neighbors, metadata) is stored after
   *   the header and accessed via explicit offsets.
   *
   * This design intentionally allows:
   * - Appending additional fields after the existing layout
   * - Extending layouts via inheritance without breaking compatibility
   * - Safe reuse of the same underlying buffer across derived views
   *
   * In other words:
   *   base_layout | extension_1 | extension_2 | ...
   *
   * As long as the base header remains unchanged, derived layouts can
   * safely interpret and extend the memory span.
   */

  // =====================================================================

  /*
   * Root node layout (serialized on tape_)
   *
   * | hnsw_build_params | max_level | entry_point |
   *
   * - hnsw_build_params:
   *   Global build-time parameters for the HNSW index (M, efConstruction, etc).
   *
   * - max_level (level_t):
   *   The highest level currently present in the graph.
   *
   * - entry_point (slot_id_t):
   *   Slot identifier of the entry node at max_level.
   *   - storage_kind::disk   -> OID
   *   - storage_kind::memory -> std::size_t
   *
   * This structure represents the minimal persistent metadata required
   * to bootstrap traversal of the HNSW graph.
   */
  class root_t
  {
    protected:
      byte_t *tape_ {};

    public:
      using header_type_t = hnsw_build_params;

      explicit root_t (byte_t *tape) noexcept : tape_ (tape) {}
      byte_t *tape() const noexcept
      {
	return tape_;
      }

      explicit operator bool() const noexcept
      {
	return tape_;
      }

      root_t() = default;

      // delete copy constructor and assignment operator
      root_t (root_t const &) = delete;
      root_t &operator= (root_t const &) = delete;

      // implement move
      root_t (root_t &&) noexcept = default;
      root_t &operator= (root_t &&) noexcept = default;

      static constexpr std::size_t offset_params = 0;
      static constexpr std::size_t offset_level = sizeof (header_type_t);
      static constexpr std::size_t offset_entry = offset_level + sizeof (level_t);

      header_type_t get_params () const noexcept
      {
	return misaligned_load<header_type_t> (tape_);
      }
      void set_params (header_type_t v) noexcept
      {
	return misaligned_store<header_type_t> (tape_, v);
      }

      level_t get_level() const noexcept
      {
	return misaligned_load<level_t> (tape_ + offset_level);
      }
      void set_level (level_t v) noexcept
      {
	return misaligned_store<level_t> (tape_ + offset_level, v);
      }

      slot_id_t get_entry() const noexcept
      {
	return misaligned_load<slot_id_t> (tape_ + offset_entry);
      }
      void set_entry (slot_id_t v) noexcept
      {
	return misaligned_store<slot_id_t> (tape_ + offset_entry, v);
      }

      static constexpr std::size_t get_size() noexcept
      {
	return sizeof (header_type_t) + sizeof (level_t) + sizeof (slot_id_t);
      }

      std::string dump() const noexcept
      {
	std::stringstream ss;
	ss << "params: " << get_params() << ", level: " << get_level() << ", entry: " << dump_oid (get_entry());
	return ss.str();
      }

      friend std::ostream &operator<< (std::ostream &os, const root_t &root)
      {
	os << root.dump();
	return os;
      }
  };

  /*
  * Graph node layout (serialized on tape_)
  *
  * | key | level | neighbors_offset | vector | neighbors... |
  *
  * - key (OID):
  *   Logical identifier of the indexed object.
  *   This is the primary reference used to map graph nodes back to
  *   database tuples.
  *
  * - level (level_t):
  *   Maximum HNSW level of this node.
  *
  * - flags (uint32_t):
  *   Node metadata flags. Currently used to mark tombstoned entries.
  *
  * - neighbors_offset (std::size_t):
  *   Byte offset from tape_ to the beginning of the neighbors array.
  *   Allows variable-sized vectors without fixing the header layout.
  *
  * - vector (float[dim]):
  *   Stored embedding vector (raw, already normalized if required).
  *
  * - neighbors:
  *   Adjacency lists for each level, stored separately and accessed
  *   via neighbors_ref_t.
  *
  * Notes:
  * - The header is fixed-size; vector and neighbors are variable-size.
  * - neighbors_offset decouples vector dimension from graph topology.
  */
  class node_t
  {
    protected:
      byte_t *tape_ {};

    public:

      explicit node_t (byte_t *tape) noexcept : tape_ (tape)
      {
	//
      }
      byte_t *tape() const noexcept
      {
	return tape_;
      }
      byte_t *vector_tape () const noexcept
      {
	return tape_ + offset_vector;
      }
      byte_t *neighbors_tape() const noexcept
      {
	return tape_ + get_neighbors_offset ();
      }
      explicit operator bool() const noexcept
      {
	return tape_;
      }

      node_t() = default;

      // delete copy constructor and assignment operator
      node_t (node_t const &) = delete;
      node_t &operator= (node_t const &) = delete;

      // implement move
      node_t (node_t &&) noexcept = default;
      node_t &operator= (node_t &&) noexcept = default;

      using flags_t = uint32_t;

      static constexpr flags_t FLAG_NONE = 0;
      static constexpr flags_t FLAG_TOMBSTONE = 1u;

      static constexpr std::size_t offset_key = 0;
      static constexpr std::size_t offset_level = offset_key + sizeof (slot_id_t);
      static constexpr std::size_t offset_flags = offset_level + sizeof (level_t);
      static constexpr std::size_t offset_neighbors_offset = offset_flags + sizeof (flags_t);
      static constexpr std::size_t offset_vector = offset_neighbors_offset + sizeof (std::size_t);
      static constexpr std::size_t offset_header_end = offset_vector;

      OID get_key() const noexcept
      {
	return misaligned_load<OID> (tape_);
      }
      void set_key (OID v) noexcept
      {
	return misaligned_store<OID> (tape_, v);
      }

      level_t get_level() const noexcept
      {
	return misaligned_load<level_t> (tape_ + offset_level);
      }
      void set_level (level_t v) noexcept
      {
	return misaligned_store<level_t> (tape_ + offset_level, v);
      }

      flags_t get_flags () const noexcept
      {
	return misaligned_load<flags_t> (tape_ + offset_flags);
      }

      void set_flags (flags_t flags) noexcept
      {
	return misaligned_store<flags_t> (tape_ + offset_flags, flags);
      }

      bool is_tombstoned () const noexcept
      {
	return (get_flags () & FLAG_TOMBSTONE) != 0;
      }

      void set_tombstoned (bool tombstoned) noexcept
      {
	flags_t flags = get_flags ();

	if (tombstoned)
	  {
	    flags |= FLAG_TOMBSTONE;
	  }
	else
	  {
	    flags &= ~FLAG_TOMBSTONE;
	  }

	set_flags (flags);
      }

      const float *get_vector() const noexcept
      {
	return reinterpret_cast<const float *> (vector_tape());
      }
      void set_vector (const float *v, std::size_t dim) noexcept
      {
	std::size_t offset = offset_header_end + sizeof (float) * dim;
	set_neighbors_offset (offset);
	std::memcpy (vector_tape(), v, dim * sizeof (float));
      }

      /*
      * neighbors_offset:
      *
      * - Enables variable-length vectors without duplicating node headers.
      * - Allows future extension (e.g., quantized vectors, metadata blocks).
      * - Keeps neighbor lists naturally aligned after vector storage.
      */
      std::size_t get_neighbors_offset () const noexcept
      {
	return misaligned_load<std::size_t> (tape_ + offset_neighbors_offset);
      }

      void set_neighbors_offset (std::size_t offset) noexcept
      {
	return misaligned_store<std::size_t> (tape_ + offset_neighbors_offset, offset);
      }

      static constexpr std::size_t get_size (std::size_t dim, std::size_t neighbors_count) noexcept
      {
	return offset_vector + sizeof (float) * dim;
      }

      std::string dump() const noexcept
      {
	std::stringstream ss;
	ss << "key: " << dump_oid (get_key()) << ", level: " << get_level() << ", neighbors_offset: " << get_neighbors_offset();
	return ss.str();
      }

      friend std::ostream &operator<< (std::ostream &os, const node_t &node)
      {
	os << node.dump();
	return os;
      }
  };

  /*
  * Neighbor list layout (serialized on tape_)
  *
  * | count | slot_0 | slot_1 | ... | slot_(count-1) |
  *
  * - count (neighbors_count_t):
  *   Number of valid neighbor entries.
  *
  * - slot_i (slot_id_t):
  *   Slot identifiers of adjacent nodes.
  *   Interpretation depends on storage backend:
  *     - disk   -> OID
  *     - memory -> std::size_t
  *
  * Notes:
  * - Compact, contiguous memory layout for cache efficiency.
  * - No pointers: fully relocatable and disk-friendly.
  * - Erase and push_back operate in-place.
  */
  class neighbors_ref_t
  {
    protected:
      byte_t *tape_ {nullptr};

      static constexpr std::size_t shift (std::size_t i = 0) noexcept
      {
	return sizeof (neighbors_count_t) + sizeof (slot_id_t) * i;
      }

    public:

      explicit neighbors_ref_t (byte_t *tape) noexcept : tape_ (tape) {}
      byte_t *tape() const noexcept
      {
	return tape_;
      }
      explicit operator bool() const noexcept
      {
	return tape_;
      }

      neighbors_ref_t() = default;

      // delete copy constructor and assignment operator
      neighbors_ref_t (neighbors_ref_t const &) = delete;
      neighbors_ref_t &operator= (neighbors_ref_t const &) = delete;

      // implement move
      neighbors_ref_t (neighbors_ref_t &&) noexcept = default;
      neighbors_ref_t &operator= (neighbors_ref_t &&) noexcept = default;

      std::size_t size() const noexcept
      {
	return misaligned_load<neighbors_count_t> (tape_);
      }
      void clear() noexcept
      {
	neighbors_count_t n = misaligned_load<neighbors_count_t> (tape_);
	std::memset (tape_, 0, shift (n));
	misaligned_store<neighbors_count_t> (tape_, 0);
      }
      void push_back (slot_id_t slot) noexcept
      {
	neighbors_count_t n = misaligned_load<neighbors_count_t> (tape_);
	misaligned_store<slot_id_t> (tape_ + shift (n), slot);
	misaligned_store<neighbors_count_t> (tape_, n + 1);
      }

      slot_id_t at (std::size_t index) const noexcept
      {
	assert (index < size());
	return misaligned_load<slot_id_t> (tape_ + shift (index));
      }

      template <typename allow_slot_at> std::size_t erase_if (allow_slot_at &&allow_slot) noexcept
      {
	std::size_t old_count = misaligned_load<neighbors_count_t> (tape_);
	std::size_t removed_count = 0;
	for (std::size_t i = 0; i < old_count; ++i)
	  {
	    slot_id_t slot = misaligned_load<slot_id_t> (tape_ + shift (i));
	    if (allow_slot (slot))
	      {
		removed_count++;
	      }
	    else
	      {
		misaligned_store<slot_id_t> (tape_ + shift (i - removed_count), slot);
	      }
	  }
	misaligned_store<neighbors_count_t> (tape_, old_count - removed_count);
	return removed_count;
      }

      std::string dump() const noexcept
      {
	std::stringstream ss;
	std::size_t n = size();
	ss << "size: " << n << "\n";
	ss << " [";
	for (std::size_t i = 0; i < n; ++i)
	  {
	    std::string dump_str = dump_slot (at (i));
	    if (dump_str == "0|0|0")
	      {
		fprintf (stdout, "size: %zu\n", n);
		abort();
	      }
	    ss << dump_str;
	    if (i < n - 1)
	      {
		ss << ", ";
	      }
	  }
	ss << "]";
	return ss.str();
      }

      friend std::ostream &operator<< (std::ostream &os, const neighbors_ref_t &neighbors_ref)
      {
	os << neighbors_ref.dump();
	return os;
      }
  };
}
