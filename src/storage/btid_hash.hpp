#ifndef _HNSW_BTID_STUB_HPP_
#define _HNSW_BTID_STUB_HPP_

#include "storage_common.h"

namespace detail_hnsw_btid_hash_eq
{
  inline std::size_t hash_combine (std::size_t seed, std::size_t v)
  {
    // 64-bit friendly hash combine
    seed ^= v + 0x9e3779b97f4a7c15ULL + (seed<<6) + (seed>>2);
    return seed;
  }
} // namespace detail_hnsw_btid_hash_eq

// Some builds of CUBRID define BTID roughly as { VFID vfid; PAGEID root_pageid; } where
// VFID contains { int volid; int fileid; }. We provide a conservative == and hash that
// rely on those common fields. Adjust field names here if your platform differs.
inline bool operator== (const BTID &a, const BTID &b) noexcept
{
  return a.vfid.volid == b.vfid.volid
	 && a.vfid.fileid == b.vfid.fileid
	 && a.root_pageid == b.root_pageid;
}

namespace std
{
  template<> struct hash<BTID>
  {
    size_t operator() (const BTID &b) const noexcept
    {
      using namespace detail_hnsw_btid_hash_eq;
      std::size_t h = 0;
      h = hash_combine (h, std::hash<int>() (b.vfid.volid));
      h = hash_combine (h, std::hash<int>() (b.vfid.fileid));
      h = hash_combine (h, std::hash<int>() (b.root_pageid));
      return h;
    }
  };
}

#endif // _HNSW_BTID_STUB_HPP_