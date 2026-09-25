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

#include "config.h"
#include "boot_sr.h"
#include "critical_section.h"
#include "error_manager.h"
#include "file_manager.h"
#include "log_impl.h"
#include "log_manager.h"
#include "message_catalog.h"
#include "page_buffer.h"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include "tz_support.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <unistd.h>
#include <vector>
#include <thread>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  THREAD_ENTRY *fixture_thread = nullptr;

  bool boot (const char *database)
  {
    if (er_init (nullptr, ER_NEVER_EXIT) != NO_ERROR)
      {
	return false;
      }
    cubthread::initialize (fixture_thread);
    if (!fixture_thread || msgcat_init () != NO_ERROR || tz_load () != NO_ERROR)
      {
	return false;
      }
    if (sysprm_load_and_init (database, nullptr, SYSPRM_LOAD_ALL) != NO_ERROR)
      {
	return false;
      }
    if (sync_initialize_sync_stats () != NO_ERROR || csect_initialize_static_critical_sections () != NO_ERROR)
      {
	return false;
      }
    CHECK_ARGS checks = {true, true};
    if (boot_restart_server (fixture_thread, false, database, false, &checks, nullptr, true) != NO_ERROR)
      {
	return false;
      }
    TRAN_STATE state;
    return logtb_assign_tran_index (fixture_thread, NULL_TRANID, TRAN_ACTIVE, nullptr, &state,
				    TRAN_LOCK_INFINITE_WAIT, TRAN_READ_COMMITTED) != NULL_TRAN_INDEX;
  }

  void reply (const char *state, const VPID &vpid)
  {
    std::printf ("PGFIXTURE {\"state\":\"%s\",\"volid\":%d,\"pageid\":%d}\n", state, vpid.volid, vpid.pageid);
    std::fflush (stdout);
  }

  bool owns_write_fix (PAGE_PTR page, const VPID &vpid)
  {
    // Allocation gave this single-threaded owner a WRITE fix, which has not
    // been released. These native checks also exist in release builds.
    return page && pgbuf_get_fix_count (page) == 1
	   && pgbuf_get_latch_mode (page) == PGBUF_LATCH_WRITE
	   && VPID_EQ (pgbuf_get_vpid_ptr (page), &vpid);
  }

  bool absent_without_loading (const VPID &vpid)
  {
    er_clear ();
    PAGE_PTR page = pgbuf_fix (fixture_thread, &vpid, OLD_PAGE_IF_IN_BUFFER,
			       PGBUF_LATCH_READ, PGBUF_CONDITIONAL_LATCH);
    if (page)
      {
	pgbuf_unfix (fixture_thread, page);
	return false;
      }
    // A latch/validation error is not evidence of a hash miss.
    return er_errid () == NO_ERROR;
  }

  bool replace_page (const VFID &file, const VPID &target, int &allocated, std::vector<VPID> &workload)
  {
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (15);
    PAGE_TYPE kind = PAGE_QRESULT;
    // The controller configures 4096 buffers. Check only between whole-pool
    // batches: a successful cache-only hit can promote the target in the LRU.
    for (allocated = 0; allocated < 32768;)
      {
	for (int i = 0; i < 4096; ++i)
	  {
	    if (std::chrono::steady_clock::now () >= deadline)
	      {
		return false;
	      }
	    VPID vpid;
	    PAGE_PTR page = nullptr;
	    if (file_alloc (fixture_thread, &file, file_init_temp_page_type, &kind, &vpid, &page) != NO_ERROR)
	      {
		return false;
	      }
	    bool flushed = pgbuf_flush_with_wal (fixture_thread, page) != nullptr;
	    workload.push_back (vpid);
	    pgbuf_unfix (fixture_thread, page);
	    if (!flushed)
	      {
		return false;
	      }
	    ++allocated;
	  }
	if (absent_without_loading (target))
	  {
	    return true;
	  }
      }
    return false;
  }

  bool remove_workload (const VPID &target, const std::vector<VPID> &workload)
  {
    // Only after target eviction is independently proven, remove the other
    // private temporary pages so a complete bounded scan can finish in time.
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
    for (const auto &vpid : workload)
      {
	if (VPID_EQ (&vpid, &target))
	  {
	    return false;
	  }
	bool reported_retry = false;
	for (;;)
	  {
	    if (std::chrono::steady_clock::now () >= deadline)
	      {
		return false;
	      }
	    er_clear ();
	    PAGE_PTR page = pgbuf_fix (fixture_thread, &vpid, OLD_PAGE_IF_IN_BUFFER,
				       PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH);
	    if (page)
	      {
		if (pgbuf_invalidate (fixture_thread, page) != NO_ERROR)
		  {
		    return false;
		  }
	      }
	    else if (er_errid () != NO_ERROR)
	      {
		return false;
	      }
	    if (absent_without_loading (vpid))
	      {
		break;
	      }
	    // Invalidation may legally decline a currently protected victim.
	    // Retry within the same cleanup deadline; only a native miss wins.
	    if (!reported_retry)
	      {
		std::fprintf (stderr, "cleanup retry: VPID=%d|%d still cached after invalidation\n", vpid.volid, vpid.pageid);
		reported_retry = true;
	      }
	    std::this_thread::yield ();
	  }
      }
    return true;
  }

  bool prepare_page_kinds (const VFID &file, std::vector<VPID> &workload)
  {
    // Private temporary pages carry real native header tags, without claiming
    // to be usable instances of those storage structures. No other owner fixes them.
    const PAGE_TYPE kinds[] = {PAGE_FTAB, PAGE_HEAP, PAGE_VOLHEADER, PAGE_VOLBITMAP,
			       PAGE_QRESULT, PAGE_EHASH, PAGE_OVERFLOW, PAGE_AREA,
			       PAGE_CATALOG, PAGE_BTREE, PAGE_LOG, PAGE_DROPPED_FILES, PAGE_VACUUM_DATA
			      };
    const char *names[] = {"ftab", "heap", "volheader", "volbitmap", "qresult", "ehash", "overflow",
			   "area", "catalog", "btree", "log", "dropped_files", "vacuum_data"
			  };
    std::vector<VPID> pages;
    for (PAGE_TYPE kind : kinds)
      {
	VPID vpid;
	PAGE_PTR page = nullptr;
	if (file_alloc (fixture_thread, &file, file_init_temp_page_type, &kind, &vpid, &page) != NO_ERROR)
	  {
	    return false;
	  }
	bool valid = page && pgbuf_get_page_ptype (fixture_thread, page) == kind;
	workload.push_back (vpid);
	pages.push_back (vpid);
	pgbuf_unfix (fixture_thread, page);
	if (!valid)
	  {
	    return false;
	  }
      }
    std::printf ("PGFIXTURE {\"state\":\"page-kinds\",\"pages\":[");
    for (std::size_t i = 0; i < pages.size (); ++i)
      {
	std::printf ("%s{\"volid\":%d,\"pageid\":%d,\"page_kind\":\"%s\"}",
		     i == 0 ? "" : ",", pages[i].volid, pages[i].pageid, names[i]);
      }
    std::printf ("]}\n");
    std::fflush (stdout);
    return true;
  }

  bool read_command (char (&command)[32])
  {
    // Bound the whole command, including a controller that sends only a prefix.
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
    for (std::size_t length = 0; length + 1 < sizeof (command);)
      {
	auto remaining = std::chrono::duration_cast<std::chrono::milliseconds> (
				 deadline - std::chrono::steady_clock::now ()).count ();
	if (remaining <= 0)
	  {
	    return false;
	  }
	pollfd input {STDIN_FILENO, POLLIN, 0};
	int ready = poll (&input, 1, static_cast<int> (remaining));
	if (ready < 0 && errno == EINTR)
	  {
	    continue;
	  }
	if (ready <= 0)
	  {
	    return false;
	  }
	ssize_t count = read (STDIN_FILENO, &command[length], 1);
	if (count < 0 && errno == EINTR)
	  {
	    continue;
	  }
	if (count != 1)
	  {
	    return false;
	  }
	if (command[length++] == '\n')
	  {
	    command[length] = '\0';
	    return true;
	  }
      }
    return false;
  }
}

// Test-only executable: normal engine boot and producer daemon, private stdin
// synchronization. It is neither installed nor exposed as a production command.
int main (int argc, char **argv)
{
  const bool permanent = argc == 3 && std::strcmp (argv[2], "--permanent") == 0;
  if ((argc != 2 && !permanent) || !boot (argv[1]))
    {
      std::fprintf (stderr, "fixture boot failed: %d\n", er_errid ());
      return 1;
    }
  VFID file;
  VFID_SET_NULL (&file);
  VFID permanent_file;
  VFID_SET_NULL (&permanent_file);
  VPID target;
  VPID_SET_NULL (&target);
  PAGE_PTR held = nullptr;
  PAGE_TYPE kind = PAGE_QRESULT;
  int result = 1;
  std::vector<VPID> workload;
  // A private permanent allocation lets the actual consumer address the target
  // in its inspected volume set. No SQL/index owner can refix this page.
  bool target_file_ready = !permanent
			   || file_create_with_npages (fixture_thread, FILE_BTREE_OVERFLOW_KEY, 1,
			       nullptr, &permanent_file) == NO_ERROR;
  PAGE_TYPE target_kind = permanent ? PAGE_OVERFLOW : PAGE_QRESULT;
  if (file_create_temp (fixture_thread, 1, &file) != NO_ERROR
      || !target_file_ready
      || file_alloc (fixture_thread, permanent ? &permanent_file : &file,
		     permanent ? file_init_page_type : file_init_temp_page_type, &target_kind, &target, &held) != NO_ERROR
      || !held || !pgbuf_flush_with_wal (fixture_thread, held) || !owns_write_fix (held, target))
    {
      std::fprintf (stderr, "fixture allocation/clean flush failed: %d\n", er_errid ());
    }
  else
    {
      reply ("clean-held", target);
      for (;;)
	{
	  char command[32];
	  if (!read_command (command))
	    {
	      break;
	    }
	  if (std::strcmp (command, "dirty\n") == 0 && held)
	    {
	      const char canary[] = "pgbuf-fixture-private-page-bytes";
	      std::memcpy (held, canary, sizeof (canary));
	      pgbuf_set_dirty (fixture_thread, held, DONT_FREE);
	      if (!owns_write_fix (held, target))
		{
		  break;
		}
	      reply ("dirty-held", target);
	    }
	  else if (std::strcmp (command, "held\n") == 0 && held)
	    {
	      if (!owns_write_fix (held, target))
		{
		  break;
		}
	      reply ("held", target);
	    }
	  else if (std::strcmp (command, "page-kinds\n") == 0 && held)
	    {
	      if (!prepare_page_kinds (file, workload))
		{
		  break;
		}
	    }
	  else if (std::strcmp (command, "populate\n") == 0)
	    {
	      bool populated = true;
	      for (int i = 0; i < 512; ++i)
		{
		  VPID page_id;
		  PAGE_PTR page = nullptr;
		  if (file_alloc (fixture_thread, &file, file_init_temp_page_type, &kind, &page_id, &page) != NO_ERROR)
		    {
		      populated = false;
		      break;
		    }
		  workload.push_back (page_id);
		  pgbuf_unfix (fixture_thread, page);
		}
	      if (!populated)
		{
		  break;
		}
	      reply ("populated", target);
	    }

	  else if (std::strcmp (command, "evict\n") == 0 && held)
	    {
	      if (!pgbuf_flush_with_wal (fixture_thread, held))
		{
		  break;
		}
	      pgbuf_unfix (fixture_thread, held);
	      held = nullptr;
	      int allocated = 0;
	      if (!replace_page (file, target, allocated, workload))
		{
		  break;
		}
	      std::fprintf (stderr, "native replacement: allocated=%d, cache-only target miss before workload cleanup\n", allocated);
	      if (!remove_workload (target, workload) || !absent_without_loading (target))
		{
		  break;
		}
	      workload.clear ();
	      reply ("evicted", target);
	    }
	  else if (std::strcmp (command, "absent\n") == 0 && !held)
	    {
	      // No workload runs while waiting for commands. This private
	      // page has no other owner that could refix it during a scan.
	      if (!absent_without_loading (target))
		{
		  break;
		}
	      reply ("absent", target);
	    }
	  else if (std::strcmp (command, "stop\n") == 0)
	    {
	      result = 0;
	      break;
	    }
	  else
	    {
	      break;
	    }
	}
    }
  if (held)
    {
      pgbuf_unfix (fixture_thread, held);
    }
  if (!VFID_ISNULL (&file) && file_temp_retire (fixture_thread, &file) != NO_ERROR)
    {
      result = 1;
    }
  int transaction = LOG_FIND_THREAD_TRAN_INDEX (fixture_thread);
  if (transaction > LOG_SYSTEM_TRAN_INDEX)
    {
      // Abort also undoes the private permanent allocation, through the normal
      // recovery system operation required by permanent file destruction.
      log_abort (fixture_thread, transaction);
      logtb_release_tran_index (fixture_thread, transaction);
    }
  xboot_shutdown_server (fixture_thread, ER_THREAD_FINAL);
  if (result != 0)
    {
      std::fprintf (stderr, "fixture precondition/synchronization failed: %d\n", er_errid ());
    }
  return result;
}
