#pragma once

#include "hnsw_utils.hpp"
#include "hnsw_api.hpp"

namespace cubhnsw
{
  using level_t = int16_t;
  using byte_t = std::byte;
  using neighbors_count_t = uint32_t;

  constexpr level_t MAX_LEVELS = 16;

  static std::string dump_oid (const OID &oid)
  {
    return std::string (std::to_string (oid.volid)) + "|" + std::string (std::to_string (oid.pageid)) + "|" + std::string (
		   std::to_string (oid.slotid));
  }

  template <typename T>
  static inline std::string dump_slot (const T &v)
  {
    if constexpr (std::is_integral_v<T>)
      {
	return std::to_string (v);
      }
    else
      {
	std::ostringstream oss;
	oss << v;
	return oss.str ();
      }
  }

  template <>
  inline std::string dump_slot<OID> (const OID &oid)
  {
    return dump_oid (oid);
  }

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

  template <class ID_TRAITS>
  class node_t
  {
    protected:
      byte_t *tape_ {};

    public:
      using slot_id_t = typename ID_TRAITS::slot_id_t;

      explicit node_t (byte_t *tape) noexcept : tape_ (tape)
      {
#if !defined (NDEBUG)
	//std::string dump_str = dump ();
	//fprintf (stdout, "node_t: %s\n", dump_str.c_str());
#endif
      }
      byte_t *tape() const noexcept
      {
	return tape_;
      }
      byte_t *neighbors_tape() const noexcept
      {
	return tape_ + offset_neighbors;
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

      static constexpr std::size_t offset_key = 0;
      static constexpr std::size_t offset_vec_slot = sizeof (OID);
      static constexpr std::size_t offset_level = offset_vec_slot + sizeof (slot_id_t);
      static constexpr std::size_t offset_neighbors = offset_level + sizeof (level_t);

      OID get_key() const noexcept
      {
	return misaligned_load<OID> (tape_);
      }
      void set_key (OID v) noexcept
      {
	return misaligned_store<OID> (tape_, v);
      }

      slot_id_t get_vec_slot() const noexcept
      {
	return misaligned_load<slot_id_t> (tape_ + offset_vec_slot);
      }
      void set_vec_slot (slot_id_t v) noexcept
      {
	return misaligned_store<slot_id_t> (tape_ + offset_vec_slot, v);
      }

      level_t get_level() const noexcept
      {
	return misaligned_load<level_t> (tape_ + offset_level);
      }
      void set_level (level_t v) noexcept
      {
	return misaligned_store<level_t> (tape_ + offset_level, v);
      }

      static constexpr std::size_t get_size() noexcept
      {
	return offset_neighbors;
      }

      std::string dump() const noexcept
      {
	std::stringstream ss;
	ss << "key: " << dump_oid (get_key()) << ", vec_slot: " << dump_slot (get_vec_slot()) << ", level: " << get_level();
	return ss.str();
      }

      friend std::ostream &operator<< (std::ostream &os, const node_t &node)
      {
	os << node.dump();
	return os;
      }
  };

  template <class ID_TRAITS>
  class neighbors_ref_t
  {
    protected:
      byte_t *tape_ {nullptr};

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
