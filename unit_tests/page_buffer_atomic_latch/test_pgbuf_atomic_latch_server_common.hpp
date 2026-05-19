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

/* in-process SERVER_MODE boot for atomic_latch tests */

#ifndef _TEST_PGBUF_ATOMIC_LATCH_SERVER_COMMON_HPP_
#define _TEST_PGBUF_ATOMIC_LATCH_SERVER_COMMON_HPP_

#include "gtest/gtest.h"
#include <cstdio>
#include <cstdlib>

#include "boot_sr.h"
#include "critical_section.h"
#include "error_manager.h"
#include "log_impl.h"
#include "log_manager.h"
#include "message_catalog.h"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include "tz_support.h"

#include "log_volids.hpp"
#include "page_buffer.h"
#include "storage_common.h"
#include "file_manager.h"

/* memory_wrapper.hpp NOT included here — its #define new breaks <future> placement-new */

extern THREAD_ENTRY *g_thread_p;

/* allocate a fresh tran_index on the calling thread; pair with worker_release_tran_index */
inline int
worker_assign_tran_index (THREAD_ENTRY *thread_p)
{
  /* worker thread_entry inherits whatever er_msg state was left by a previous user;
   * scrub before AND after to keep heap_*'s `er_errid_if_has_error () == NO_ERROR` invariant. */
  er_clear ();
  TRAN_STATE state;
  int tran_idx = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE,
					  NULL, &state,
					  TRAN_LOCK_INFINITE_WAIT, TRAN_READ_COMMITTED);
  er_clear ();
  return tran_idx;
}

inline void
worker_release_tran_index (THREAD_ENTRY *thread_p, int tran_index)
{
  if (tran_index != NULL_TRAN_INDEX)
    {
      logtb_release_tran_index (thread_p, tran_index);
    }
}

class ServerModeEnv : public ::testing::Environment
{
  public:
    void SetUp () override
    {
      std::printf ("##### Booting SERVER_MODE for heap-insert interrupt unfix-hole tests #####\n");

      int err;

      err = er_init (NULL, ER_NEVER_EXIT);
      if (err != NO_ERROR)
	{
	  std::fprintf (stderr, "er_init failed: %d\n", err);
	  std::abort ();
	}

      cubthread::initialize (g_thread_p);
      if (g_thread_p == nullptr)
	{
	  std::fprintf (stderr, "cubthread::initialize failed\n");
	  std::abort ();
	}

      err = msgcat_init ();
      if (err != NO_ERROR)
	{
	  std::fprintf (stderr, "msgcat_init failed: %d\n", err);
	  std::abort ();
	}

      err = tz_load ();
      if (err != NO_ERROR)
	{
	  std::fprintf (stderr, "tz_load failed: %d\n", err);
	  std::abort ();
	}

      (void) sysprm_load_and_init ("unittestdb", NULL, SYSPRM_LOAD_ALL);

      err = sync_initialize_sync_stats ();
      if (err != NO_ERROR)
	{
	  std::fprintf (stderr, "sync_initialize_sync_stats failed: %d\n", err);
	  std::abort ();
	}

      err = csect_initialize_static_critical_sections ();
      if (err != NO_ERROR)
	{
	  std::fprintf (stderr, "csect_initialize_static_critical_sections failed: %d\n", err);
	  std::abort ();
	}

      CHECK_ARGS check_args = { true, true };
      err = boot_restart_server (g_thread_p, false, "unittestdb", false, &check_args, NULL, true);
      if (err != NO_ERROR)
	{
	  std::fprintf (stderr, "boot_restart_server failed: %d\n", err);
	  std::abort ();
	}

      TRAN_STATE tran_state;
      int tran_idx = logtb_assign_tran_index (g_thread_p, NULL_TRANID, TRAN_ACTIVE,
					      NULL, &tran_state,
					      TRAN_LOCK_INFINITE_WAIT, TRAN_READ_COMMITTED);
      if (tran_idx == NULL_TRAN_INDEX)
	{
	  std::fprintf (stderr, "logtb_assign_tran_index failed\n");
	  std::abort ();
	}

      std::printf ("SERVER_MODE booted (tran_index=%d)\n", tran_idx);
    }

    void TearDown () override
    {
      std::printf ("##### Stopping SERVER_MODE #####\n");
      std::fflush (stdout);

      /* CTest FIXTURES_CLEANUP runs cubrid server stop + deletedb out of band */
    }
};

#endif /* _TEST_PGBUF_ATOMIC_LATCH_SERVER_COMMON_HPP_ */
