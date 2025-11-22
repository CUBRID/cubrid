#include "hnsw_storage_disk.hpp"

#include "oid.h"

#include "file_manager.h" // FILE_DESCRIPTORS
#include "overflow_file.h"

namespace cubhnsw
{
  static int
  create_continous_vector_file (THREAD_ENTRY *thread_p, const VFID &vfid)
  {
    int error_code = NO_ERROR;
    FILE_DESCRIPTORS des;

    memset (&des, 0, sizeof (des));

  error_code = file_create_with_npages (thread_p, FILE_BTREE, 1, &des, (VFID*) &vfid);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  log_sysop_start (thread_p);

#if 0 // TODO: TDE
  error_code = heap_get_class_tde_algorithm (thread_p, &btid->topclass_oid, &tde_algo);
  if (error_code != NO_ERROR)
    {
      VFID_SET_NULL (&btid->ovfid);
      return error_code;
    }
  error_code = file_apply_tde_algorithm (thread_p, &btid->ovfid, tde_algo);
  if (error_code != NO_ERROR)
    {
      VFID_SET_NULL (&btid->ovfid);
      return error_code;
    }
#endif
  return error_code;
  }


  

  disk_storage::disk_storage (
	  const BTID &giid,
	  const hnsw_build_params &build_params)
    : base (giid, build_params)
  {
    m_vfid = giid.vfid;
    m_root_vpid = VPID { giid.root_pageid, giid.vfid.volid };
  }

  disk_storage::~disk_storage ()
  {
    
  }

      // The root is not initialized yet
      bool 
      disk_storage::is_empty ()
      {
        return false;
      }

      // not yet
      void 
      disk_storage::init_root (std::byte *root_block, std::size_t &root_size)
      {
        root_disk_t<disk_traits_t> root { reinterpret_cast<byte_t *> (root_block) };

        // TODO: error handling
        (void) create_continous_vector_file (m_thread_p, m_vec_pool_vfid);
        root.set_vec_pool_vfid(m_vec_pool_vfid);

        root_size = root.get_size();
      }

      disk_storage::slot_id_t 
      disk_storage::add_vector (const OID &key, const float *vector)
      {
        std::size_t dim = this->get_dimension ();
        std::size_t bytes = dim * sizeof (float);

        char* rec_buf = (char*) vector;
        int rec_buf_size = (int) bytes;




        return m_root_vpid.pageid;
      }

      void
      disk_storage::alloc_vector_page (VPID &vpid, PAGE_PTR &page_ptr)
      {
        PAGE_TYPE ptype = PAGE_BTREE;
        (void) file_alloc (m_thread_p, &m_vec_pool_vfid, file_init_page_type, &ptype, &vpid, &page_ptr);
      }

      disk_storage::slot_id_t 
      disk_storage::add_node (const OID &key, const level_t &level)
      {
        return {};
      }

      disk_storage::pinned_t 
      disk_storage::get_root (lock_mode mode)
      {
        return disk_storage::pinned_t {this, disk_storage::slot_id_t {}, nullptr, lock_mode::none,std::nullopt};
      }
      /*
      disk_storage::pinned_t 
      disk_storage::get_node (const OID &key, lock_mode mode)
      {
        return disk_storage::pinned_t {this, disk_storage::slot_id_t {}, nullptr, lock_mode::none,std::nullopt};
      }
        */

      disk_storage::pinned_t 
      disk_storage::get_node_by_slot_id (const slot_id_t &id, const lock_mode &mode)
      {
        return disk_storage::pinned_t {this, disk_storage::slot_id_t {}, nullptr, lock_mode::none,std::nullopt};
      }

      disk_storage::pinned_t 
      disk_storage::get_node_by_key (const OID &key, const lock_mode &mode)
      {
        return disk_storage::pinned_t {this, disk_storage::slot_id_t {}, nullptr, lock_mode::none,std::nullopt};
      }

      disk_storage::pinned_t 
      disk_storage::get_neighbors (const slot_id_t &id, const level_t &level,
				      const lock_mode &mode)
              {
                return disk_storage::pinned_t {this, disk_storage::slot_id_t {}, nullptr, lock_mode::none,std::nullopt};
              }

      disk_storage::pinned_t 
      disk_storage::get_vector (const OID &key, const lock_mode &mode)
      {
        return pinned_t {this, slot_id_t {}, nullptr, lock_mode::none,std::nullopt};
      }

      // promote lockmode from shared to exclusive
      disk_storage::pinned_t 
      disk_storage::promote_root (pinned_t &old)
      {
        return pinned_t (this, old.id(), old.data(), lock_mode::exclusive, std::nullopt);
      }
}
