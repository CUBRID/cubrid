/*
 * Copyright 2008 Search Solution Corporation
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
 * test_oos_server_common.hpp - SERVER_MODE boot infrastructure for OOS unit tests
 *
 * Boots a CUBRID server in-process using the same init sequence as cub_server
 * (net_server_start), minus the network layer. This gives tests direct access
 * to server-internal APIs (page buffer, heap, OOS, vacuum) under full
 * SERVER_MODE threading and MVCC infrastructure.
 */

#ifndef _TEST_OOS_SERVER_COMMON_HPP_
#define _TEST_OOS_SERVER_COMMON_HPP_

#include "gtest/gtest.h"
#include <cstdio>
#include <cstring>
#include <string>

/* Server boot infrastructure (mirrors net_server_start init sequence) */
#include "boot_sr.h"
#include "critical_section.h"
#include "error_manager.h"
#include "log_impl.h"
#include "log_manager.h"
#include "message_catalog.h"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include "tz_support.h"

/* OOS / storage APIs */
#include "oos_file.hpp"
#include "page_buffer.h"
#include "record_descriptor.hpp"
#include "slotted_page.h"
#include "storage_common.h"
#include "file_manager.h"
#include "heap_file.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Global thread entry — set during ServerModeEnv::SetUp(). */
static THREAD_ENTRY *thread_p;

/*
 * ServerModeEnv - GoogleTest global environment for SERVER_MODE tests.
 *
 * Boots a CUBRID server in-process following the same sequence as
 * net_server_start() in src/communication/network_sr.c, but skips the
 * network layer (net_server_init / css_initialize_server_interfaces / css_init).
 *
 * Requires: "unittestdb" created by the OOS_DB CTest fixture (cubrid createdb).
 */
class ServerModeEnv : public ::testing::Environment
{
  public:
    void SetUp () override
    {
      printf ("##### Starting SERVER_MODE Server For OOS Vacuum Testing #####\n");

      int err;

      /* 1. Error manager (initial) */
      err = er_init (NULL, ER_NEVER_EXIT);
      if (err != NO_ERROR)
	{
	  fprintf (stderr, "er_init failed: %d\n", err);
	  abort ();
	}

      /* 2. Thread infrastructure — gives us thread_p */
      cubthread::initialize (thread_p);
      if (thread_p == nullptr)
	{
	  fprintf (stderr, "cubthread::initialize failed: thread_p is null\n");
	  abort ();
	}

      /* 3. Message catalog */
      err = msgcat_init ();
      if (err != NO_ERROR)
	{
	  fprintf (stderr, "msgcat_init failed: %d\n", err);
	  abort ();
	}

      /* 4. Timezone data */
      err = tz_load ();
      if (err != NO_ERROR)
	{
	  fprintf (stderr, "tz_load failed: %d\n", err);
	  abort ();
	}

      /* 5. System parameters */
      (void) sysprm_load_and_init ("unittestdb", NULL, SYSPRM_LOAD_ALL);

      /* 6. Sync stats */
      err = sync_initialize_sync_stats ();
      if (err != NO_ERROR)
	{
	  fprintf (stderr, "sync_initialize_sync_stats failed: %d\n", err);
	  abort ();
	}

      /* 7. Critical sections */
      err = csect_initialize_static_critical_sections ();
      if (err != NO_ERROR)
	{
	  fprintf (stderr, "csect_initialize_static_critical_sections failed: %d\n", err);
	  abort ();
	}

      /* 8. Boot the server.
       *    skip_vacuum = true: we control vacuum manually in tests. */
      CHECK_ARGS check_args = { true, true };
      err = boot_restart_server (thread_p, false, "unittestdb", false, &check_args, NULL, true);
      if (err != NO_ERROR)
	{
	  fprintf (stderr, "boot_restart_server failed: %d\n", err);
	  abort ();
	}

      /* 9. Assign a worker transaction so sysop (oos_create_file etc.) is allowed.
       *    After boot, thread_p has tran_index == LOG_SYSTEM_TRAN_INDEX (0),
       *    but is_allowed_sysop() requires tran_index > 0 (active worker). */
      TRAN_STATE tran_state;
      int tran_idx = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE,
					      NULL, &tran_state,
					      TRAN_LOCK_INFINITE_WAIT, TRAN_READ_COMMITTED);
      if (tran_idx == NULL_TRAN_INDEX)
	{
	  fprintf (stderr, "logtb_assign_tran_index failed\n");
	  abort ();
	}

      printf ("SERVER_MODE server booted successfully (tran_index=%d)\n", tran_idx);
    }

    void TearDown () override
    {
      printf ("##### Stopping SERVER_MODE Server #####\n");

      /* Release the worker transaction assigned in SetUp before shutdown. */
      int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      if (tran_index > LOG_SYSTEM_TRAN_INDEX)
	{
	  (void) log_abort (thread_p, tran_index);
	  logtb_release_tran_index (thread_p, tran_index);
	}

      (void) xboot_shutdown_server (thread_p, ER_THREAD_FINAL);
      fflush (stdout);
    }
};

// ============================================================================
// Utility functions (mode-independent, shared with SA_MODE tests)
// ============================================================================

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

  /* Wraps a test RECDES as the oos_buffer src that oos_insert expects. */
  inline int
  oos_insert_from_recdes (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const RECDES &recdes, OID &oid)
  {
    return oos_insert (thread_p, oos_vfid, oos_buffer (recdes.data, static_cast<std::size_t> (recdes.length)), oid);
  }

  /* Reads OID into a fresh RECDES, sized via oos_get_length (tests have no heap-inline length). */
  inline int
  oos_read_with_alloc (THREAD_ENTRY *thread_p, const OID &oid, RECDES &recdes)
  {
    recdes = RECDES{};
    int len = oos_get_length (thread_p, oid);
    if (len < 0)
      {
	return er_errid ();
      }
    int err = recdes_allocate_data_area (&recdes, len);
    if (err != NO_ERROR)
      {
	return err;
      }
    err = oos_read (thread_p, oid, oos_buffer (recdes.data, static_cast<std::size_t> (len)));
    if (err != NO_ERROR)
      {
	recdes_free_data_area (&recdes);
	return err;
      }
    recdes.length = len;
    return NO_ERROR;
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

} /* namespace test_oos_utils */

#endif /* _TEST_OOS_SERVER_COMMON_HPP_ */
