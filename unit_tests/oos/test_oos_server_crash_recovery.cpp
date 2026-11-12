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
 * test_oos_server_crash_recovery.cpp - SERVER_MODE crash in the middle of an UPDATE commit's postpones
 *
 * ADR-0008 (A'): a committed MVCC UPDATE publishes the chains it dropped with one RVOOS_UPDATE_DROP_POSTPONE per
 * row; the commit's postpone appends RVOOS_NOTIFY_VACUUM under the UPDATE's MVCCID, and vacuum reclaims the
 * chains. A crash after some of those postpones ran leaves the rest to restart recovery, which appends the
 * remaining notifications before vacuum is booted, and vacuum then rebuilds the lost log blocks from the log.
 *
 * Each step runs in its own child process, so a process that boots the database is the only one that has it
 * open: this process never boots it.
 *   1. A child loads rows with OOS values, updates every row to new values, and commits; the commit stops the
 *      process after the second drop postpone appended its notification (the log is flushed up to it).
 *   2. A second child boots: restart recovery reruns the remaining drop postpones, vacuum rebuilds the lost
 *      blocks, and vacuum must reclaim every chain the UPDATE dropped and none it wrote.
 *
 * CBRD-27925: the growth reuses an emptied OOS page in place; when the page's dead ANCHORED slots leave no room
 * for the record it reinitializes the page first, logged redo-only (RVPGBUF_NEW_PAGE). A crash after that log
 * record is flushed, with the inserting transaction still open, must replay the reinitialization, undo the
 * insert, and leave the page an empty OOS page that the next growth reuses.
 */

#include "test_oos_server_common.hpp"

#include <chrono>
#include <functional>
#include <thread>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "mvcc.h"
#include "file_manager.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "vacuum.h"
#include "xserver_interface.h"

#include "memory_wrapper.hpp"

/* bridge functions to access static functions in oos_file.cpp */
int bridge_oos_get_max_chunk_size_within_page ();
void bridge_oos_debug_counters_reset ();
oos_debug_counters bridge_oos_debug_counters_get ();
int bridge_oos_stats_del_bestspace_by_vfid (THREAD_ENTRY *thread_p, const VFID *vfid);
OOS_HDR_STATS *bridge_oos_get_header_stats_ptr (THREAD_ENTRY *thread_p, PAGE_PTR page_header);

namespace
{
  const char *const DB_NAME = "oosservercrashdb";
  const int NUM_ROWS = 4;
  const int CRASH_AFTER_NOTIFICATIONS = 2;
  const int VALUE_SIZE = 4096;

  /* Exit codes of the children. */
  const int EXIT_CRASHED_IN_POSTPONE = 42;
  const int EXIT_COMMIT_RETURNED = 43;
  const int EXIT_BOOT_FAILED = 11;
  const int EXIT_SETUP_FAILED = 12;
  const int EXIT_OLD_CHAIN_LEFT = 21;
  const int EXIT_NEW_CHAIN_LOST = 22;
  const int EXIT_CRASHED_AFTER_REINIT = 44;
  const int EXIT_PAGE_NOT_OOS = 31;
  const int EXIT_PAGE_NOT_EMPTY = 32;
  const int EXIT_FILE_PAGES_CHANGED = 33;
  const int EXIT_REUSE_FAILED = 34;
  const int EXIT_VALUE_LOST = 35;
  const int EXIT_FILE_GREW = 36;

  struct crash_layout
  {
    HFID hfid;
    OID class_oid;
    OID heap_oids[NUM_ROWS];
    OID old_oos_oids[NUM_ROWS];
    OID new_oos_oids[NUM_ROWS];
  };

  const int HEAP_HDR_SIZE = 8;	/* OR_MVCC_REP_SIZE + OR_CHN_SIZE */
  const int VOT_ENTRY_SZ = 4;	/* OR_INT_SIZE (4-byte offset mode) */
  const int MVCC_HEADER_SPARE = 2 * OR_MVCCID_SIZE;

  /* SERVER_MODE boot of DB_NAME, as ServerModeEnv does for unittestdb. */
  int
  boot_server ()
  {
    int err = er_init (NULL, ER_NEVER_EXIT);
    if (err != NO_ERROR)
      {
	return err;
      }
    cubthread::initialize (thread_p);
    if (thread_p == nullptr || msgcat_init () != NO_ERROR || tz_load () != NO_ERROR)
      {
	return ER_FAILED;
      }
    (void) sysprm_load_and_init (DB_NAME, NULL, SYSPRM_LOAD_ALL);
    if (sync_initialize_sync_stats () != NO_ERROR || csect_initialize_static_critical_sections () != NO_ERROR)
      {
	return ER_FAILED;
      }
    CHECK_ARGS check_args = { true, true };
    err = boot_restart_server (thread_p, false, DB_NAME, false, &check_args, NULL, true);
    if (err != NO_ERROR)
      {
	return err;
      }
    TRAN_STATE tran_state;
    int tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE, NULL, &tran_state,
		     TRAN_LOCK_INFINITE_WAIT, TRAN_READ_COMMITTED);
    return tran_index == NULL_TRAN_INDEX ? ER_FAILED : NO_ERROR;
  }

  void
  shutdown_server ()
  {
    int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    if (tran_index > LOG_SYSTEM_TRAN_INDEX)
      {
	(void) log_commit (thread_p, tran_index, false);
	logtb_release_tran_index (thread_p, tran_index);
      }
    (void) xboot_shutdown_server (thread_p, ER_THREAD_FINAL);
  }

  bool
  commit_current_tran ()
  {
    return xtran_server_commit (thread_p, false) == TRAN_UNACTIVE_COMMITTED;
  }

  /* A heap record of db_user's representation whose first variable attribute is an OOS stub for oos_oid, the
   * layout test_oos_real_vacuum_server.cpp builds. */
  int
  build_heap_recdes_with_oos (const OID &class_oid, const OID &oos_oid, INT64 oos_length, RECDES &rec_out)
  {
    int cache_index = -1;
    OR_CLASSREP *repr = heap_classrepr_get (thread_p, &class_oid, nullptr, NULL_REPRID, &cache_index);
    if (repr == nullptr)
      {
	return ER_FAILED;
      }
    const int n_var = repr->n_variable;
    const int repr_id = repr->id;
    heap_classrepr_free_and_init (repr, &cache_index);
    if (n_var < 1)
      {
	return ER_FAILED;
      }

    const int vot_bytes = (n_var + 1) * VOT_ENTRY_SZ;
    const int total = HEAP_HDR_SIZE + vot_bytes + OR_OOS_INLINE_SIZE;
    int err = recdes_allocate_data_area (&rec_out, total + MVCC_HEADER_SPARE);
    if (err != NO_ERROR)
      {
	return err;
      }
    rec_out.type = REC_HOME;
    rec_out.length = total;
    std::memset (rec_out.data, 0, total);

    char *base = rec_out.data;
    OR_PUT_INT (base + OR_REP_OFFSET, repr_id | (OR_RECORD_FLAG_HAS_OOS << OR_RECORD_FLAG_SHIFT_BITS)
		| OR_OFFSET_SIZE_4BYTE);
    char *vot = base + HEAP_HDR_SIZE;
    OR_PUT_INT (vot, OR_SET_VAR_OOS (vot_bytes));
    for (int i = 1; i <= n_var; ++i)
      {
	const int offset = vot_bytes + OR_OOS_INLINE_SIZE;
	OR_PUT_INT (vot + i * VOT_ENTRY_SZ, i == n_var ? OR_SET_VAR_LAST_ELEMENT (offset) : offset);
      }

    char *slot = vot + vot_bytes;
    OR_PUT_OID (slot, &oos_oid);
    OR_PUT_BIGINT (slot + OR_OID_SIZE, &oos_length);
    LOG_LSA identity_stamp = NULL_LSA;
    err = oos_get_identity_stamp (thread_p, oos_oid, &identity_stamp);
    if (err != NO_ERROR)
      {
	recdes_free_data_area (&rec_out);
	return err;
      }
    INT64 packed_identity_stamp = oos_pack_identity_stamp (identity_stamp);
    OR_PUT_BIGINT (slot + OR_OID_SIZE + OR_BIGINT_SIZE, &packed_identity_stamp);
    return NO_ERROR;
  }

  int
  build_plain_heap_recdes (int data_size, RECDES &rec_out)
  {
    const int total = HEAP_HDR_SIZE + data_size;
    int err = recdes_allocate_data_area (&rec_out, total + MVCC_HEADER_SPARE);
    if (err != NO_ERROR)
      {
	return err;
      }
    rec_out.type = REC_HOME;
    rec_out.length = total;
    std::memset (rec_out.data, 0, total);
    OR_PUT_INT (rec_out.data + OR_REP_OFFSET, OR_OFFSET_SIZE_4BYTE);
    return NO_ERROR;
  }

  void
  start_scan_cache (HEAP_SCANCACHE &scan_cache, HFID &hfid, const OID &class_oid)
  {
    heap_scancache_quick_start_with_class_hfid (thread_p, &scan_cache, &hfid);
    COPY_OID (&scan_cache.node.class_oid, &class_oid);
    scan_cache.file_type = FILE_HEAP;
    scan_cache.mvcc_disabled_class = false;
    scan_cache.page_latch = PGBUF_LATCH_WRITE;
    scan_cache.cache_last_fix_page = false;
  }

  int
  insert_oos_value (const VFID &oos_vfid, char fill, OID &oos_oid_out)
  {
    RECDES oos_rec {};
    int err = test_oos_utils::from_string_into_recdes (std::string (VALUE_SIZE, fill), oos_rec);
    if (err != NO_ERROR)
      {
	return err;
      }
    err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos_rec, oos_oid_out);
    recdes_free_data_area (&oos_rec);
    return err;
  }

  bool
  oos_unreadable (const OID &oos_oid)
  {
    RECDES out {};
    int err = test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, out);
    if (out.data != nullptr)
      {
	recdes_free_data_area (&out);
      }
    er_clear ();
    return err != NO_ERROR;
  }

  /* Step 1: load, update, and commit; the commit stops the process in its postpones. */
  void
  crash_in_update_commit (int layout_fd)
  {
    crash_layout layout {};
    if (boot_server () != NO_ERROR)
      {
	std::_Exit (EXIT_BOOT_FAILED);
      }
    if (xlocator_find_class_oid (thread_p, "db_user", &layout.class_oid, NULL_LOCK) != LC_CLASSNAME_EXIST
	|| xheap_create (thread_p, &layout.hfid, &layout.class_oid, false) != NO_ERROR)
      {
	std::_Exit (EXIT_SETUP_FAILED);
      }
    VFID oos_vfid = VFID_INITIALIZER;
    if (!heap_oos_find_vfid (thread_p, &layout.hfid, &oos_vfid, true) || !commit_current_tran ())
      {
	std::_Exit (EXIT_SETUP_FAILED);
      }

    HEAP_SCANCACHE scan_cache;
    start_scan_cache (scan_cache, layout.hfid, layout.class_oid);
    for (int i = 0; i < NUM_ROWS; i++)
      {
	RECDES heap_rec {};
	if (insert_oos_value (oos_vfid, 'a' + i, layout.old_oos_oids[i]) != NO_ERROR
	    || build_heap_recdes_with_oos (layout.class_oid, layout.old_oos_oids[i], VALUE_SIZE, heap_rec) != NO_ERROR)
	  {
	    std::_Exit (EXIT_SETUP_FAILED);
	  }
	HEAP_OPERATION_CONTEXT ctx;
	heap_create_insert_context (&ctx, &layout.hfid, &layout.class_oid, &heap_rec, &scan_cache);
	int err = heap_insert_logical (thread_p, &ctx, NULL);
	layout.heap_oids[i] = ctx.res_oid;
	recdes_free_data_area (&heap_rec);
	if (err != NO_ERROR)
	  {
	    std::_Exit (EXIT_SETUP_FAILED);
	  }
      }
    if (!commit_current_tran ())
      {
	std::_Exit (EXIT_SETUP_FAILED);
      }

    for (int i = 0; i < NUM_ROWS; i++)
      {
	RECDES heap_rec {};
	if (insert_oos_value (oos_vfid, 'A' + i, layout.new_oos_oids[i]) != NO_ERROR
	    || build_heap_recdes_with_oos (layout.class_oid, layout.new_oos_oids[i], VALUE_SIZE, heap_rec) != NO_ERROR)
	  {
	    std::_Exit (EXIT_SETUP_FAILED);
	  }
	HEAP_OPERATION_CONTEXT ctx;
	heap_create_update_context (&ctx, &layout.hfid, &layout.heap_oids[i], &layout.class_oid, &heap_rec,
				    &scan_cache, UPDATE_INPLACE_NONE);
	int err = heap_update_logical (thread_p, &ctx);
	recdes_free_data_area (&heap_rec);
	if (err != NO_ERROR)
	  {
	    std::_Exit (EXIT_SETUP_FAILED);
	  }
      }
    (void) heap_scancache_end (thread_p, &scan_cache);

    if (write (layout_fd, &layout, sizeof (layout)) != (ssize_t) sizeof (layout))
      {
	std::_Exit (EXIT_SETUP_FAILED);
      }
    oos_test_exit_after_update_drop_notifications (CRASH_AFTER_NOTIFICATIONS, EXIT_CRASHED_IN_POSTPONE);
    (void) commit_current_tran ();
    std::_Exit (EXIT_COMMIT_RETURNED);
  }

  /* Step 2: restart, let vacuum drain, and check which chains are left. */
  void
  recover_and_vacuum (const crash_layout &layout_in)
  {
    crash_layout layout = layout_in;
    if (boot_server () != NO_ERROR)
      {
	std::_Exit (EXIT_BOOT_FAILED);
      }

    HEAP_SCANCACHE scan_cache;
    start_scan_cache (scan_cache, layout.hfid, layout.class_oid);

    /* Vacuum takes a block only once the log moved two blocks past it. */
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (180);
    const VACUUM_LOG_BLOCKID start_block = vacuum_get_log_blockid (log_Gl.prior_info.prior_lsa.pageid);
    auto all_old_gone = [&layout] ()
    {
      for (int i = 0; i < NUM_ROWS; i++)
	{
	  if (!oos_unreadable (layout.old_oos_oids[i]))
	    {
	      return false;
	    }
	}
      return true;
    };
    while (!all_old_gone () && std::chrono::steady_clock::now () < deadline)
      {
	if (vacuum_get_log_blockid (log_Gl.prior_info.prior_lsa.pageid) <= start_block + 2)
	  {
	    RECDES filler {};
	    OID dummy = OID_INITIALIZER;
	    if (build_plain_heap_recdes (4096, filler) != NO_ERROR)
	      {
		std::_Exit (EXIT_SETUP_FAILED);
	      }
	    HEAP_OPERATION_CONTEXT ctx;
	    heap_create_insert_context (&ctx, &layout.hfid, &layout.class_oid, &filler, &scan_cache);
	    int err = heap_insert_logical (thread_p, &ctx, NULL);
	    recdes_free_data_area (&filler);
	    if (err != NO_ERROR || !commit_current_tran ())
	      {
		std::_Exit (EXIT_SETUP_FAILED);
	      }
	  }
	else
	  {
	    (void) vacuum_wakeup_master_daemon ();
	    std::this_thread::sleep_for (std::chrono::milliseconds (100));
	  }
      }
    (void) heap_scancache_end (thread_p, &scan_cache);

    if (!all_old_gone ())
      {
	std::_Exit (EXIT_OLD_CHAIN_LEFT);
      }
    for (int i = 0; i < NUM_ROWS; i++)
      {
	if (oos_unreadable (layout.new_oos_oids[i]))
	  {
	    std::_Exit (EXIT_NEW_CHAIN_LOST);
	  }
      }

    (void) xheap_destroy (thread_p, &layout.hfid, &layout.class_oid);
    shutdown_server ();
    std::_Exit (0);
  }

  /* Layout the reinit crash child hands to the restarting child. */
  struct reinit_layout
  {
    VFID oos_vfid;
    VPID dead_slot_vpid;
    int user_pages;
  };

  const int NUM_DEAD_SLOTS = 3;	/* three dead ANCHORED slots leave no room for a maximum-size chunk */

  int
  count_user_pages (const VFID &oos_vfid)
  {
    OOS_STATS_INFO info;
    if (oos_get_stats_by_vfid (thread_p, oos_vfid, &info) != NO_ERROR)
      {
	return -1;
      }
    return info.num_user_pages;
  }

  /* The set of emptied pages and the bestspace hints die with the process, so only the in-process state of the
   * crashing child needs clearing: without it the insert takes the page through the hint and not the growth. */
  bool
  clear_bestspace_hints (const VFID &oos_vfid)
  {
    (void) bridge_oos_stats_del_bestspace_by_vfid (thread_p, &oos_vfid);

    VPID hdr_vpid = VPID_INITIALIZER;
    if (file_get_sticky_first_page (thread_p, &oos_vfid, &hdr_vpid) != NO_ERROR)
      {
	return false;
      }
    PAGE_PTR hdr_page = pgbuf_fix (thread_p, &hdr_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
    if (hdr_page == nullptr)
      {
	return false;
      }
    OOS_HDR_STATS *oos_hdr = bridge_oos_get_header_stats_ptr (thread_p, hdr_page);
    if (oos_hdr == nullptr)
      {
	pgbuf_unfix_and_init (thread_p, hdr_page);
	return false;
      }
    for (int i = 0; i < OOS_NUM_BEST_SPACESTATS; i++)
      {
	VPID_SET_NULL (&oos_hdr->estimates.best[i].vpid);
	oos_hdr->estimates.best[i].freespace = 0;
	VPID_SET_NULL (&oos_hdr->estimates.second_best[i]);
      }
    oos_hdr->estimates.num_second_best = 0;
    oos_hdr->estimates.head_second_best = 0;
    oos_hdr->estimates.tail_second_best = 0;
    oos_hdr->estimates.num_other_high_best = 0;
    LOG_DATA_ADDR addr;
    addr.vfid = NULL;
    addr.pgptr = hdr_page;
    addr.offset = 0;
    log_skip_logging (thread_p, &addr);
    pgbuf_set_dirty (thread_p, hdr_page, FREE);
    return true;
  }

  /* A string that stores exactly one chunk of the maximum size. */
  std::string
  largest_chunk_string ()
  {
    return test_oos_utils::make_repeated_pattern_string (bridge_oos_get_max_chunk_size_within_page () - 1);
  }

  /* Step 1: empty a page that keeps dead slots, then let an open transaction's growth reinitialize it and insert
   * into it, and stop the process with the log flushed through the insert and the transaction uncommitted. */
  void
  crash_after_reinit_before_commit (int layout_fd)
  {
    reinit_layout layout {};
    if (boot_server () != NO_ERROR || oos_create_file (thread_p, layout.oos_vfid) != NO_ERROR)
      {
	std::_Exit (EXIT_BOOT_FAILED);
      }

    OID small_oids[NUM_DEAD_SLOTS + 1];
    for (OID &oid : small_oids)
      {
	RECDES rec {};
	if (test_oos_utils::from_string_into_recdes (test_oos_utils::make_repeated_pattern_string (100), rec)
	    != NO_ERROR)
	  {
	    std::_Exit (EXIT_SETUP_FAILED);
	  }
	int err = test_oos_utils::oos_insert_from_recdes (thread_p, layout.oos_vfid, rec, oid);
	recdes_free_data_area (&rec);
	if (err != NO_ERROR || oid.pageid != small_oids[0].pageid)
	  {
	    std::_Exit (EXIT_SETUP_FAILED);
	  }
      }
    for (const OID &oid : small_oids)
      {
	if (test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, layout.oos_vfid, oid) != NO_ERROR)
	  {
	    std::_Exit (EXIT_SETUP_FAILED);
	  }
      }
    layout.dead_slot_vpid.volid = small_oids[0].volid;
    layout.dead_slot_vpid.pageid = small_oids[0].pageid;
    layout.user_pages = count_user_pages (layout.oos_vfid);
    if (!commit_current_tran () || layout.user_pages != 2 || !clear_bestspace_hints (layout.oos_vfid))
      {
	std::_Exit (EXIT_SETUP_FAILED);
      }

    const std::string largest = largest_chunk_string ();
    RECDES rec {};
    if (test_oos_utils::from_string_into_recdes (largest, rec) != NO_ERROR)
      {
	std::_Exit (EXIT_SETUP_FAILED);
      }
    bridge_oos_debug_counters_reset ();
    OID large_oid = OID_INITIALIZER;
    if (test_oos_utils::oos_insert_from_recdes (thread_p, layout.oos_vfid, rec, large_oid) != NO_ERROR
	|| bridge_oos_debug_counters_get ().reclaim_growth_reinit_pages != 1ULL
	|| large_oid.pageid != layout.dead_slot_vpid.pageid || count_user_pages (layout.oos_vfid) != 2)
      {
	/* The scenario did not reach the reinitialization of the dead-slot page. */
	std::_Exit (EXIT_SETUP_FAILED);
      }
    recdes_free_data_area (&rec);

    if (write (layout_fd, &layout, sizeof (layout)) != (ssize_t) sizeof (layout))
      {
	std::_Exit (EXIT_SETUP_FAILED);
      }
    /* The reinitialization and the insert are in the log on disk; the transaction never commits and no data page
     * is written by this process. */
    logpb_force_flush_pages (thread_p);
    std::_Exit (EXIT_CRASHED_AFTER_REINIT);
  }

  /* Step 2: recovery has replayed the reinitialization and undone the insert. The page must be an empty OOS page,
   * and the next maximum-size insert must take it instead of growing the file. */
  void
  recover_and_reuse_reinitialized_page (const reinit_layout &layout)
  {
    if (boot_server () != NO_ERROR)
      {
	std::_Exit (EXIT_BOOT_FAILED);
      }

    PAGE_PTR page = pgbuf_fix (thread_p, &layout.dead_slot_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			       PGBUF_UNCONDITIONAL_LATCH);
    if (page == nullptr || pgbuf_get_page_ptype (thread_p, page) != PAGE_OOS)
      {
	std::_Exit (EXIT_PAGE_NOT_OOS);
      }
    const int records = spage_number_of_records (page);
    pgbuf_unfix_and_init (thread_p, page);
    if (records != 0)
      {
	std::_Exit (EXIT_PAGE_NOT_EMPTY);
      }
    if (count_user_pages (layout.oos_vfid) != layout.user_pages)
      {
	std::_Exit (EXIT_FILE_PAGES_CHANGED);
      }

    const std::string largest = largest_chunk_string ();
    RECDES rec {};
    if (test_oos_utils::from_string_into_recdes (largest, rec) != NO_ERROR)
      {
	std::_Exit (EXIT_SETUP_FAILED);
      }
    OID reuse_oid = OID_INITIALIZER;
    if (test_oos_utils::oos_insert_from_recdes (thread_p, layout.oos_vfid, rec, reuse_oid) != NO_ERROR)
      {
	std::_Exit (EXIT_REUSE_FAILED);
      }
    recdes_free_data_area (&rec);
    if (count_user_pages (layout.oos_vfid) != layout.user_pages || reuse_oid.pageid != layout.dead_slot_vpid.pageid)
      {
	std::_Exit (EXIT_FILE_GREW);
      }
    RECDES out {};
    if (test_oos_utils::oos_read_with_alloc (thread_p, reuse_oid, out) != NO_ERROR
	|| std::string (out.data) != largest)
      {
	std::_Exit (EXIT_VALUE_LOST);
      }
    recdes_free_data_area (&out);

    VFID oos_vfid = layout.oos_vfid;
    (void) oos_remove_file (thread_p, oos_vfid);
    (void) commit_current_tran ();
    shutdown_server ();
    std::_Exit (0);
  }

  int
  wait_child (pid_t pid)
  {
    int status = -1;
    if (pid < 0 || waitpid (pid, &status, 0) != pid)
      {
	return -1;
      }
    if (WIFSIGNALED (status))
      {
	return 128 + WTERMSIG (status);
      }
    return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
  }
}

TEST (OosServerCrashRecovery, CrashInUpdateCommitPostponesThenRestartReclaimsOnlyTheDroppedChains)
{
  int fds[2];
  ASSERT_EQ (pipe (fds), 0);

  fflush (NULL);
  pid_t pid = fork ();
  if (pid == 0)
    {
      close (fds[0]);
      crash_in_update_commit (fds[1]);
    }
  close (fds[1]);
  const int crashed = wait_child (pid);
  crash_layout layout {};
  const ssize_t got = read (fds[0], &layout, sizeof (layout));
  close (fds[0]);
  ASSERT_EQ (crashed, EXIT_CRASHED_IN_POSTPONE)
      << "the committing child did not stop in its drop postpones (" << EXIT_COMMIT_RETURNED
      << ": the commit returned; " << EXIT_BOOT_FAILED << ": boot; " << EXIT_SETUP_FAILED << ": setup)";
  ASSERT_EQ (got, (ssize_t) sizeof (layout));

  fflush (NULL);
  pid = fork ();
  if (pid == 0)
    {
      recover_and_vacuum (layout);
    }
  const int recovered = wait_child (pid);
  EXPECT_EQ (recovered, 0) << "restart and vacuum: " << EXIT_BOOT_FAILED << ": boot (recovery) failed; "
			   << EXIT_OLD_CHAIN_LEFT << ": a dropped chain was not reclaimed; " << EXIT_NEW_CHAIN_LOST
			   << ": a chain the UPDATE wrote was lost; 134: abort (assert)";
}

TEST (OosServerCrashRecovery, CrashAfterEmptyPageReinitBeforeCommitThenRestartKeepsTheEmptyPageForReuse)
{
  int fds[2];
  ASSERT_EQ (pipe (fds), 0);

  fflush (NULL);
  pid_t pid = fork ();
  if (pid == 0)
    {
      close (fds[0]);
      crash_after_reinit_before_commit (fds[1]);
    }
  close (fds[1]);
  const int crashed = wait_child (pid);
  reinit_layout layout {};
  const ssize_t got = read (fds[0], &layout, sizeof (layout));
  close (fds[0]);
  ASSERT_EQ (crashed, EXIT_CRASHED_AFTER_REINIT)
      << "the child did not reach the reinitialization of the dead-slot page (" << EXIT_BOOT_FAILED << ": boot; "
      << EXIT_SETUP_FAILED << ": setup or the insert did not reinitialize the page)";
  ASSERT_EQ (got, (ssize_t) sizeof (layout));

  fflush (NULL);
  pid = fork ();
  if (pid == 0)
    {
      recover_and_reuse_reinitialized_page (layout);
    }
  const int recovered = wait_child (pid);
  EXPECT_EQ (recovered, 0) << "restart: " << EXIT_BOOT_FAILED << ": boot (recovery) failed; " << EXIT_PAGE_NOT_OOS
			   << ": the page is not an OOS page; " << EXIT_PAGE_NOT_EMPTY << ": the page is not empty; "
			   << EXIT_FILE_PAGES_CHANGED << ": the file's page count changed; " << EXIT_REUSE_FAILED
			   << ": the insert failed; " << EXIT_FILE_GREW << ": the insert did not reuse the page; "
			   << EXIT_VALUE_LOST << ": the value read back differs; 134: abort (assert)";
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  return RUN_ALL_TESTS ();
}
