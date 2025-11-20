#pragma once

#include "hnsw_storage.hpp"
#include "hnsw_storage_mem.hpp"
#include "hnsw_storage_disk.hpp"

namespace cubhnsw {

class hnsw_storage_factory
{
public:
    template <storage_kind Kind>
    using traits_t = storage_traits<Kind>;

    template <storage_kind Kind>
    using storage_base_t = hnsw_storage_base<traits_t<Kind>>;

    template <storage_kind Kind>
    using mem_t = hnsw_storage_memory<traits_t<Kind>>;

    template <storage_kind Kind>
    using disk_t = hnsw_storage_disk<traits_t<Kind>>;

    static std::unique_ptr<hnsw_storage_base<storage_traits<storage_kind::memory>>>
    create_memory(const BTID& giid, const hnsw_build_params& p)
    {
        return std::make_unique<mem_t<storage_kind::memory>>(giid, p);
    }

    static std::unique_ptr<hnsw_storage_base<storage_traits<storage_kind::disk>>>
    create_disk(const std::string& path, const BTID& giid, const hnsw_build_params& p)
    {
        return std::make_unique<disk_t<storage_kind::disk>>(path, giid, p);
    }
};

} // namespace cubhnsw
