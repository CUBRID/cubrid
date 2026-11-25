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
 * test_pgbuf_conditional_latch_server.cpp - SERVER_MODE tests of a refused conditional page latch request
 * (CBRD-28379)
 *
 * A conditional request (PGBUF_CONDITIONAL_LATCH) that cannot be granted is refused at once and never waits. The
 * refusal must leave no trace on the page: the BCB's waiter_exists mark tells the holder that a blocked reader or
 * writer will take the page over, so a holder that sees it skips putting the page into an LRU list on its last unfix,
 * and a non-holder READ request on a READ-latched page waits behind it. These cases check both effects through the
 * page buffer interface.
 *
 * A page buffer holder belongs to a thread entry, so the requests of "another thread" run as tasks of one-worker
 * pools. A conditional request never blocks, so each task returns as soon as its request is answered. The OOS server
 * boot and its unittestdb fixture are reused; the page under test is a freshly allocated page of an OOS file, the page
 * kind on which CBRD-28378 found the refusal.
 *
 * Whether a page entered an LRU list is read from the zone counts of pgbuf_peek_stats over the whole pool, which the
 * background threads of the server can also change. The placement cases therefore check a batch of pages at once and
 * require at least one LRU entry per page; the refusal left none.
 *
 * Not covered: the unfix fallback for queued waiters that all gave up (latch timeout or interrupt) between the
 * holder's read of waiter_exists and the wake-up. Reaching it needs a waiter stopped inside pgbuf_timed_sleep (), and
 * the page buffer has no hook for that.
 */

#include "test_oos_server_common.hpp"

#include <functional>
#include <future>
#include <vector>

#include "scope_exit.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"

namespace
{
  /* Runs functions on the thread entry of a dedicated one-worker pool, one at a time, and waits for each. */
  class other_thread
  {
    public:
      explicit other_thread (const char *name)
      {
	m_pool = thread_create_worker_pool (1, 1, name, thread_get_entry_manager ());
      }

      ~other_thread ()
      {
	thread_get_manager ()->destroy_worker_pool (m_pool);
      }

      other_thread (const other_thread &) = delete;
      other_thread &operator= (const other_thread &) = delete;

      bool is_ready () const
      {
	return m_pool != NULL;
      }

      void run (const std::function<void (THREAD_ENTRY &)> &func)
      {
	std::promise<void> done;
	std::future<void> done_future = done.get_future ();

	auto exec = [&func, &done] (cubthread::entry &context)
	{
	  /* a pool worker has no transaction of its own; act in the system transaction */
	  const int save_tran_index = context.tran_index;
	  context.tran_index = LOG_SYSTEM_TRAN_INDEX;
	  /* restore the worker and release the caller on any exit from func */
	  scope_exit finish ([&context, &done, save_tran_index] ()
	  {
	    er_clear ();
	    context.tran_index = save_tran_index;
	    done.set_value ();
	  });
	  func (context);
	};
	thread_get_manager ()->push_task (m_pool, new cubthread::entry_callable_task (std::move (exec)));
	done_future.wait ();
      }

    private:
      worker_pool_type<> *m_pool = NULL;
  };

  /* number of BCBs in any LRU zone of any LRU list */
  UINT64
  count_lru_bcbs ()
  {
    UINT64 fixed, dirty, lru1, lru2, lru3, victim_candidates, avoid_dealloc, avoid_victim, private_quota, private_cnt;
    UINT64 waiter_high, waiter_med, waiter_low, lfcq_big_prv, lfcq_prv, lfcq_shr;

    pgbuf_peek_stats (&fixed, &dirty, &lru1, &lru2, &lru3, &victim_candidates, &avoid_dealloc, &avoid_victim,
		      &private_quota, &private_cnt, &waiter_high, &waiter_med, &waiter_low, &lfcq_big_prv, &lfcq_prv,
		      &lfcq_shr);
    return lru1 + lru2 + lru3;
  }

  /* Allocates a new page in the OOS file and returns it fixed for WRITE. The page buffer took a free BCB for it, which
   * stays in the void zone, outside every LRU list, until the last unfix of its first holder. */
  PAGE_PTR
  alloc_new_page (const VFID &vfid, VPID &vpid)
  {
    PAGE_TYPE ptype = PAGE_OOS;
    PAGE_PTR page = NULL;

    if (file_alloc (thread_p, &vfid, file_init_page_type, &ptype, &vpid, &page) != NO_ERROR)
      {
	return NULL;
      }
    return page;
  }

  /* fixes the page with a conditional request and reports whether it was granted; a granted fix is released */
  bool
  try_conditional_fix (THREAD_ENTRY &entry, const VPID &vpid, PGBUF_LATCH_MODE latch_mode)
  {
    PAGE_PTR page = pgbuf_fix (&entry, &vpid, OLD_PAGE, latch_mode, PGBUF_CONDITIONAL_LATCH);
    if (page == NULL)
      {
	return false;
      }
    pgbuf_unfix_and_init (&entry, page);
    return true;
  }
}

class PgbufConditionalLatch : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      ASSERT_EQ (oos_create_file (thread_p, m_vfid), NO_ERROR);
      ASSERT_TRUE (m_other_a.is_ready ());
      ASSERT_TRUE (m_other_b.is_ready ());
    }

    VFID m_vfid;
    other_thread m_other_a { "pgbuf-test-a" };
    other_thread m_other_b { "pgbuf-test-b" };
};

/* Allocates page_count new pages and keeps them fixed for WRITE, lets another thread ask for each one with a refused
 * conditional request of latch_mode, then unfixes them. Each page must enter an LRU list on that unfix. */
static void
check_refused_requests_let_new_pages_enter_lru (const VFID &vfid, other_thread &other, PGBUF_LATCH_MODE latch_mode)
{
  constexpr int page_count = 8;
  std::vector<VPID> vpids (page_count);
  std::vector<PAGE_PTR> pages (page_count, nullptr);
  scope_exit unfix_pages ([&pages] ()
  {
    for (PAGE_PTR &page : pages)
      {
	if (page != nullptr)
	  {
	    pgbuf_unfix_and_init (thread_p, page);
	  }
      }
  });

  for (int i = 0; i < page_count; i++)
    {
      pages[i] = alloc_new_page (vfid, vpids[i]);
      ASSERT_NE (pages[i], nullptr);
    }

  const UINT64 lru_before = count_lru_bcbs ();

  int granted_count = 0;
  other.run ([&] (THREAD_ENTRY &entry)
  {
    for (const VPID &vpid : vpids)
      {
	granted_count += try_conditional_fix (entry, vpid, latch_mode) ? 1 : 0;
      }
  });
  EXPECT_EQ (granted_count, 0);

  for (PAGE_PTR &page : pages)
    {
      pgbuf_unfix_and_init (thread_p, page);
    }

  const UINT64 lru_after = count_lru_bcbs ();
  EXPECT_GE (lru_after, lru_before + page_count) << "LRU BCBs before " << lru_before << ", after " << lru_after
      << ": new pages were left outside every LRU list";
}

/* The holder of a new page unfixes it after a conditional WRITE request of another thread was refused: the page must
 * go into an LRU list. Before the fix the refusal left waiter_exists set, the holder skipped the placement, and the
 * BCB stayed in the void zone with no latch and no list, where neither victimization nor flushing can find it. */
TEST_F (PgbufConditionalLatch, RefusedWriteRequestLetsNewPageEnterLru)
{
  check_refused_requests_let_new_pages_enter_lru (m_vfid, m_other_a, PGBUF_LATCH_WRITE);
}

/* The same with a conditional READ request refused by a WRITE latch. */
TEST_F (PgbufConditionalLatch, RefusedReadRequestLetsNewPageEnterLru)
{
  check_refused_requests_let_new_pages_enter_lru (m_vfid, m_other_a, PGBUF_LATCH_READ);
}

/* A page held for READ: a conditional WRITE request of another thread is refused, and a READ request of a third
 * thread must still be granted, because no thread waits for the page. Before the fix the refusal left waiter_exists
 * set, so the READ request was treated as queued behind a waiter: refused here, blocked until the holder released
 * the page for an unconditional request. */
TEST_F (PgbufConditionalLatch, RefusedWriteRequestDoesNotBlockReaders)
{
  VPID vpid;
  PAGE_PTR page = alloc_new_page (m_vfid, vpid);
  ASSERT_NE (page, nullptr);
  pgbuf_unfix_and_init (thread_p, page);

  page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  ASSERT_NE (page, nullptr);

  bool write_granted = true;
  m_other_a.run ([&] (THREAD_ENTRY &entry)
  {
    write_granted = try_conditional_fix (entry, vpid, PGBUF_LATCH_WRITE);
  });
  EXPECT_FALSE (write_granted);

  bool read_granted = false;
  m_other_b.run ([&] (THREAD_ENTRY &entry)
  {
    read_granted = try_conditional_fix (entry, vpid, PGBUF_LATCH_READ);
  });
  EXPECT_TRUE (read_granted);

  pgbuf_unfix_and_init (thread_p, page);
}

/* Two threads hold a page for READ and one of them asks to promote its latch to WRITE with a conditional request,
 * which is refused while the other reader holds the page. A READ request of a third thread must still be granted. */
TEST_F (PgbufConditionalLatch, RefusedPromotionDoesNotBlockReaders)
{
  VPID vpid;
  PAGE_PTR page = alloc_new_page (m_vfid, vpid);
  ASSERT_NE (page, nullptr);
  pgbuf_unfix_and_init (thread_p, page);

  page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  ASSERT_NE (page, nullptr);

  bool promoted = true;
  m_other_a.run ([&] (THREAD_ENTRY &entry)
  {
    PAGE_PTR read_page = pgbuf_fix (&entry, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    ASSERT_NE (read_page, nullptr);
    promoted = try_conditional_fix (entry, vpid, PGBUF_LATCH_WRITE);
    pgbuf_unfix_and_init (&entry, read_page);
  });
  EXPECT_FALSE (promoted);

  bool read_granted = false;
  m_other_b.run ([&] (THREAD_ENTRY &entry)
  {
    read_granted = try_conditional_fix (entry, vpid, PGBUF_LATCH_READ);
  });
  EXPECT_TRUE (read_granted);

  pgbuf_unfix_and_init (thread_p, page);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerModeEnv ());
  return RUN_ALL_TESTS ();
}
