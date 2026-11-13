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

/*
 * test_oos_utils.hpp - mode-independent helpers shared by the SA_MODE and SERVER_MODE OOS unit tests
 *
 * Keep callers qualified as test_oos_utils:: because src/storage/page_buffer_util.hpp declares
 * a global ::page_auto_unfix.
 */

#ifndef _TEST_OOS_UTILS_HPP_
#define _TEST_OOS_UTILS_HPP_

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "error_manager.h"
#include "oos_file.hpp"
#include "page_buffer.h"
#include "record_descriptor.hpp"
#include "slotted_page.h"
#include "storage_common.h"

namespace test_oos_utils
{

  inline std::string
  make_repeated_pattern_string (int size)
  {
    const std::string pattern = "ABCDEFGHIJK"; /* pattern size is 11 */
    if (size <= 0)
      {
	return {};
      }

    std::string large_data;
    large_data.reserve (size);

    for (int i = 0; i < size; ++i)
      {
	large_data.push_back (pattern[i % pattern.size ()]);
      }

    return large_data;
  }

  inline int
  from_string_into_recdes (const std::string &large_data, RECDES &rec)
  {
    int err = recdes_allocate_data_area (&rec, static_cast<int> (large_data.size () + 1));
    if (err != NO_ERROR)
      {
	return err;
      }

    rec.type = REC_HOME;
    rec.length = static_cast<int> (large_data.size () + 1);

    /* copy data including null terminator */
    std::memcpy (rec.data, large_data.c_str (), large_data.size () + 1);
    return NO_ERROR;
  }

  /* Wraps a test RECDES as the oos_buffer src that oos_insert expects. */
  inline int oos_insert_from_recdes (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const RECDES &recdes, OID &oid)
  {
    return oos_insert (thread_p, oos_vfid, oos_buffer (recdes.data, static_cast<std::size_t> (recdes.length)), oid);
  }

  /* Builds the chain reference for oid from the identity stamp its head chunk currently carries.
   * Test-only shortcut: production callers take the stamp from the owning heap record's OOS inline
   * stub. Fails for an absent chunk, which keeps "reading a deleted chain errors" observable. */
  inline int oos_current_chain_ref (THREAD_ENTRY *thread_p, const OID &oid, oos_chain_ref &ref_out)
  {
    ref_out.head_oid = oid;
    ref_out.identity_stamp = NULL_LSA;
    return oos_get_identity_stamp (thread_p, oid, &ref_out.identity_stamp);
  }

  /* Deletes the chain at oid through the identity stamp its head chunk currently carries. Test-only
   * shortcut: production callers pass the reference parsed from the owning heap record's OOS inline
   * stub. A missing chunk fails inside oos_get_identity_stamp, preserving the old "deleting a gone
   * chunk errors" observable where tests relied on it (CBRD-26950). */
  inline int oos_delete_with_current_identity_stamp (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid,
      std::vector<VPID> *emptied_vpids = NULL)
  {
    oos_chain_ref ref;
    int err = oos_current_chain_ref (thread_p, oid, ref);
    if (err != NO_ERROR)
      {
	return err;
      }
    return oos_delete (thread_p, oos_vfid, ref, emptied_vpids);
  }

  /* Reads OID into a fresh RECDES, sized via oos_get_length (tests have no heap-inline length),
   * through the chain reference its head chunk currently carries. */
  inline int oos_read_with_alloc (THREAD_ENTRY *thread_p, const OID &oid, RECDES &recdes)
  {
    recdes = RECDES{};
    INT64 len = oos_get_length (thread_p, oid);
    if (len < 0)
      {
	return er_errid ();
      }
    oos_chain_ref ref;
    int err = oos_current_chain_ref (thread_p, oid, ref);
    if (err != NO_ERROR)
      {
	return err;
      }
    err = recdes_allocate_data_area (&recdes, len);
    if (err != NO_ERROR)
      {
	return err;
      }
    err = oos_read (thread_p, ref, oos_buffer (recdes.data, static_cast<std::size_t> (len)));
    if (err != NO_ERROR)
      {
	recdes_free_data_area (&recdes);
	return err;
      }
    recdes.length = len;
    return NO_ERROR;
  }

  /* RAII helpers */

  struct page_auto_unfix
  {
    THREAD_ENTRY *thread_p;
    void operator() (PAGE_PTR p) const noexcept
    {
      if (p)
	{
	  pgbuf_unfix (thread_p, p);
	}
    }
  };
  using auto_unfixed_page_ptr = std::unique_ptr<std::remove_pointer_t<PAGE_PTR>, page_auto_unfix>;
  using auto_freed_recdes_ptr = std::unique_ptr<RECDES, decltype (&recdes_free_data_area)>;

  inline int
  get_free_space_of_oid_page (THREAD_ENTRY *thread_p, const OID &oid)
  {
    VPID vpid = {oid.pageid, oid.volid};
    PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER,
				   PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (page_ptr == nullptr)
      {
	return -1;
      }
    auto_unfixed_page_ptr auto_page { page_ptr, page_auto_unfix {thread_p} };
    return spage_get_free_space (thread_p, page_ptr);
  }

} /* namespace test_oos_utils */

#endif /* _TEST_OOS_UTILS_HPP_ */
