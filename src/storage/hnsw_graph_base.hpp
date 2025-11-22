#pragma once

#include "hnsw_utils.hpp"
#include "hnsw_api.hpp"

namespace cubhnsw
{
  using level_t = int16_t;
  using byte_t = std::byte;
  using neighbors_count_t = uint32_t;

  constexpr level_t MAX_LEVELS = 16;

  // =====================================================================
  // graph
  // =====================================================================
  template <typename ID_TRAITS>
  class root_t
  {
    protected:
      byte_t *tape_ {};

    public:
      using slot_id_t = typename ID_TRAITS::slot_id_t;

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
      root_t (root_t const &) = default;
      root_t &operator= (root_t const &) = default;

      static constexpr std::size_t offset_params = 0;
      static constexpr std::size_t offset_level = sizeof (hnsw_build_params);
      static constexpr std::size_t offset_entry = offset_level + sizeof (level_t);

      misaligned_ref_gt<hnsw_build_params> get_params () const noexcept
      {
	return {tape_};
      }
      void set_params (hnsw_build_params v) noexcept
      {
	return misaligned_store<hnsw_build_params> (tape_, v);
      }

      misaligned_ref_gt<level_t> get_level() const noexcept
      {
	return {tape_ + offset_level};
      }
      void set_level (level_t v) noexcept
      {
	return misaligned_store<level_t> (tape_ + offset_level, v);
      }

      misaligned_ref_gt<slot_id_t> get_entry() const noexcept
      {
	return {tape_ + offset_entry};
      }
      void set_entry (slot_id_t v) noexcept
      {
	return misaligned_store<slot_id_t> (tape_ + offset_entry, v);
      }

      static constexpr std::size_t get_size() noexcept
      {
	return sizeof (hnsw_build_params) + sizeof (level_t) + sizeof (slot_id_t);
      }
  };

  template <class ID_TRAITS>
  class node_t
  {
    protected:
      byte_t *tape_ {};

    public:
      using slot_id_t = typename ID_TRAITS::slot_id_t;

      explicit node_t (byte_t *tape) noexcept : tape_ (tape) {}
      byte_t *tape() const noexcept
      {
	return tape_;
      }
      byte_t *neighbors_tape() const noexcept
      {
	return tape_ + node_head_bytes_();
      }
      explicit operator bool() const noexcept
      {
	return tape_;
      }

      node_t() = default;
      node_t (node_t const &) = default;
      node_t &operator= (node_t const &) = default;

      static constexpr std::size_t offset_key = 0;
      static constexpr std::size_t offset_vec_slot = sizeof (OID);
      static constexpr std::size_t offset_level = offset_vec_slot + sizeof (slot_id_t);

      misaligned_ref_gt<OID> get_key() const noexcept
      {
	return {tape_};
      }
      void set_key (OID v) noexcept
      {
	return misaligned_store<OID> (tape_, v);
      }

      misaligned_ref_gt<slot_id_t> get_vec_slot() const noexcept
      {
	return {tape_ + offset_vec_slot};
      }
      void set_vec_slot (slot_id_t v) noexcept
      {
	return misaligned_store<slot_id_t> (tape_ + offset_vec_slot, v);
      }

      misaligned_ref_gt<level_t> get_level() const noexcept
      {
	return {tape_ + offset_level};
      }
      void set_level (level_t v) noexcept
      {
	return misaligned_store<level_t> (tape_ + offset_level, v);
      }

      // from usearch
      static constexpr std::size_t node_head_bytes_() noexcept
      {
	return sizeof (slot_id_t) + sizeof (level_t);
      }
  };

  template <class ID_TRAITS>
  class neighbors_ref_t
  {
    protected:
      byte_t *tape_ {};

      static constexpr std::size_t shift (std::size_t i = 0) noexcept
      {
	return sizeof (neighbors_count_t) + sizeof (slot_id_t) * i;
      }

    public:
      using slot_id_t = typename ID_TRAITS::slot_id_t;

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
      neighbors_ref_t (neighbors_ref_t const &) = default;
      neighbors_ref_t &operator= (neighbors_ref_t const &) = default;

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
  };
}
