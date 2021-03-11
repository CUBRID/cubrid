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

#include "dbtype_def.hpp"
#include "log_recovery_redo_parallel.hpp"

#include <iomanip>
#include <sstream>

namespace cublogrecovery
{
  /**********************
   * redo_log_rec_entry_queue
   **********************/

  redo_log_rec_entry_queue::redo_log_rec_entry_queue()
    : produce_queue ( new ux_entry_deque() )
    , consume_queue ( new ux_entry_deque() )
    , adding_finished { false }
    , to_be_waited_for_op_in_progress (false)
    , dbg_stats_cons_queue_skip_count (0u)
    , sbg_stats_spin_wait_count (0u)
  {
  }

  redo_log_rec_entry_queue::~redo_log_rec_entry_queue()
  {
    assert (produce_queue->size() == 0);
    assert (consume_queue->size() == 0);
    assert (in_progress_vpids.size() == 0);

    delete produce_queue;
    produce_queue = nullptr;

    delete consume_queue;
    consume_queue = nullptr;
  }

  void redo_log_rec_entry_queue::locked_push (ux_redo_lsa_log_entry &&entry)
  {
    std::lock_guard<std::mutex> lck (produce_queue_mutex);
    produce_queue->push_back (std::move (entry));
  }

  void redo_log_rec_entry_queue::set_adding_finished()
  {
    adding_finished.store (true);
  }

  bool redo_log_rec_entry_queue::get_adding_finished() const
  {
    return adding_finished.load ();
  }

  ux_redo_lsa_log_entry redo_log_rec_entry_queue::locked_pop (bool &out_adding_finished)
  {
    std::unique_lock<std::mutex> consume_queue_lock (consume_queue_mutex);
    // stop at the barrier if an operation which needs to be waited for is in progress
    to_be_waited_for_op_in_progress_cv.wait (consume_queue_lock, [this]()
    {
      return !to_be_waited_for_op_in_progress;
    });

    out_adding_finished = false;

    // if consumption of everything in consume queue finished; see whether there's some more
    if (consume_queue->size() == 0)
      {
	// consumer queue is locked anyway, interested in not locking the producer queue
	//consume_queue = ATOMIC_TAS_ADDR (&produce_queue, consume_queue);
	std::lock_guard<std::mutex> lck (produce_queue_mutex);
	std::swap (produce_queue, consume_queue);
      }

    if (consume_queue->size() > 0)
      {
	ux_redo_lsa_log_entry first_in_consume_queue;

	{
	  // TODO: instead of every task sifting through entries locked in execution by other tasks,
	  // promote those entries as they are found to separate queues on a per-VPID basis; thus,
	  // when that specific task enters the critical section, it will first investigate these
	  // promoted queues to see if there's smth to consume from there; this has the added benefit that
	  // it will also, possibly, bundle together consecutive entries that pertain to that task, thus,
	  // further, possibly reducing contention on the happy path (ie: many consecutive same-VPID entries)
	  // and also cache localization/affinity given that the same task (presumabily scheduled on the same
	  // core) might end up consuming consecutive same-VPID entries which are applied to same location in
	  // memory

	  // TODO: for this, we need to enter here with a 'hint' VPID of what the task consumed previously

	  // access the in_progress_vpids guarded; another option to reduce contention would
	  // be to copy the contents (while guarded) and then check against the contents outside of lock
	  std::lock_guard<std::mutex> lock_in_progress_vpids (in_progress_vpids_mutex);
	  auto consume_queue_it = consume_queue->begin();
	  for (; consume_queue_it != consume_queue->end(); ++consume_queue_it)
	    {
	      const VPID it_vpid = (*consume_queue_it)->get_vpid();
	      if (in_progress_vpids.find ((it_vpid)) == in_progress_vpids.cend())
		{
		  break;
		}
	      ++dbg_stats_cons_queue_skip_count;
	    }

	  if (consume_queue_it != consume_queue->end())
	    {
	      first_in_consume_queue = std::move (*consume_queue_it);
	      consume_queue->erase (consume_queue_it);
	    }
	  else
	    {
	      // consumer will have to spin-wait
	      ++sbg_stats_spin_wait_count;
	      return nullptr;
	    }

	  // IDEA:
	  // if this is a  non-synched op, search all other entries from the current queue
	  // pertaining to the same vpid and consolidate them all in a single 'execution'; stop
	  // when all entries in the consume queue have been added or when a synched (to-be-waited-for)
	  // entry is found

	  const VPID vpid_to_be_processed = first_in_consume_queue->get_vpid();
	  assert (in_progress_vpids.find (vpid_to_be_processed) == in_progress_vpids.cend());
	  in_progress_vpids.insert (vpid_to_be_processed);
	} // unlock in_progress_vpids

	/* unlocking before this will not guarantee total ordering amongst entries' execution
	 */
	//consume_queue_lock.unlock();

	if (first_in_consume_queue->get_is_to_be_waited_for_op())
	  {
	    assert (to_be_waited_for_op_in_progress == false);
	    to_be_waited_for_op_in_progress = true;
	  }

	//consume_queue_lock.unlock();
	//to_be_waited_for_op_in_progress_cv.notify_one();

	return first_in_consume_queue;
      }
    else
      {
	// because two alternating queues are used internally, and because, when the consumption queue
	// is being almost exhausted (i.e.: there are still entries to be consumed but all of them are locked
	// by other tasks - via the in_progress_vpids), there are a few times when false negatives are returned
	// to the consumption tasks (see the 'return nullptr' in the 'then' branch); but, if control reaches here
	// it is ensured that indeed no more data exists and that no more data will be produced
	out_adding_finished = adding_finished.load();

	// if no more data will be produced (signalled by the flag), the
	// consumer will just need to terminate; otherwise, consumer is expected to
	// spin-wait and try again
	return nullptr;
      }
  }

  void redo_log_rec_entry_queue::notify_to_be_waited_for_op_finished()
  {
    assert (to_be_waited_for_op_in_progress == true);
    to_be_waited_for_op_in_progress = false;
    // notify all waiting threads, as there can be many which can pick-up work
    to_be_waited_for_op_in_progress_cv.notify_all();
  }

  void redo_log_rec_entry_queue::notify_in_progress_vpid_finished (VPID _vpid)
  {
    std::lock_guard<std::mutex> lock_in_progress_vpids (in_progress_vpids_mutex);
    assert (in_progress_vpids.find (_vpid) != in_progress_vpids.cend());
    in_progress_vpids.erase (_vpid);
  }

  /**********************
   * redo_task
   **********************/

  constexpr unsigned short redo_task::WAIT_AND_CHECK_MILLIS;

  redo_task::redo_task (std::size_t a_task_id, redo_task_active_state_bookkeeping &a_task_active_state_bookkeeping,
			redo_log_rec_entry_queue &a_queue)
    : task_id (a_task_id), task_active_state_bookkeeping (a_task_active_state_bookkeeping), queue (a_queue)
  {
    // important to set this at this moment and not when execution begins
    // to circumvent race conditions where all tasks haven't yet started work
    // while already bookkeeping is being checked
    task_active_state_bookkeeping.set_active (task_id);

    log_zip_realloc_if_needed (undo_unzip_support, LOGAREA_SIZE);
    log_zip_realloc_if_needed (redo_unzip_support, LOGAREA_SIZE);
  }

  redo_task::~redo_task()
  {
    log_zip_free_data (undo_unzip_support);
    log_zip_free_data (redo_unzip_support);
  }

  void redo_task::execute (context_type &context)
  {
    bool finished = false;

    for (; !finished ;)
      {
	bool buckets_adding_finished = false;
	auto bucket = queue.locked_pop (buckets_adding_finished);

	if (bucket == nullptr && buckets_adding_finished)
	  {
	    finished = true;
	  }
	else
	  {
	    if (bucket == nullptr)
	      {
		// TODO: check if requested to finish ourselves

		// expecting more data, sleep and check again
		std::this_thread::sleep_for (std::chrono::milliseconds (WAIT_AND_CHECK_MILLIS));
	      }
	    else
	      {
//		    redo_entry_bucket::ux_redo_lsa_log_entry_vector &bucket_entries = bucket->get_redo_lsa_log_entry_vec();

		std::stringstream dbg_ss_consume;
		dbg_ss_consume << "C: "
			       << "syn_" << bucket->get_is_to_be_waited_for_op()
			       << std::setw (4) << bucket->get_vol_id() << std::setfill ('_')
			       << std::setw (5) << bucket->get_page_id() << std::setfill (' ')
			       << " tid" << std::setw (3) << std::setfill ('_') << task_id << std::setfill (' ')
			       // << " (" << std::setw (3) << remaining_per_transaction.initial_remaining_log_entry_count << ")"
			       << "  eids:";

//		    for (ux_redo_lsa_log_entry &entry : bucket_entries)
//		      {
//			// NOTE: actual useful bit of work here
//			//std::this_thread::sleep_for (std::chrono::milliseconds (entry->get_millis()));
//			dummy_busy_wait (entry->get_millis());

//			dbg_ss_consume << std::setw (7) << entry->get_entry_id();

//			dbg_db_database->apply_changes (std::move (entry));
//		      }
		//dummy_busy_wait (bucket->get_millis ());

		// save needed data before move
		const VPID bucket_vpid = bucket->get_vpid ();
		const bool bucket_is_to_be_waited_for = bucket->get_is_to_be_waited_for_op();

		dbg_ss_consume << std::endl;

		queue.notify_in_progress_vpid_finished (bucket_vpid);

		if (bucket_is_to_be_waited_for)
		  {
		    queue.notify_to_be_waited_for_op_finished();
		  }
	      }
	  }
      }

    task_active_state_bookkeeping.set_inactive (task_id);
  }

  void redo_task::dummy_busy_wait (size_t _millis)
  {
    const auto start = std::chrono::system_clock::now();
    // declare sum outside the loop to simulate a side effect
    double sum = 0;
    while (true)
      {
	for (double sum_idx = 0.; sum_idx < 10000.; sum_idx += 1.0)
	  {
	    sum *= sum_idx;
	  }
	const std::chrono::duration<double, std::milli> diff_millis = std::chrono::system_clock::now() - start;
	if (_millis < diff_millis.count())
	  {
	    break;
	  }
      }
  }

  /**********************
   * redo_parallel
   **********************/

  redo_parallel::redo_parallel()
    : thread_manager (nullptr), worker_pool (nullptr), waited_for_termination (false)
  {
    // NOTE: already called globally (prob. during boot)
    // TODO: how can this be ensured?

    //cubthread::entry *thread_pool = nullptr;
    //cubthread::initialize (thread_pool);

    task_count = std::thread::hardware_concurrency();

    do_init_thread_manager ();
    do_init_worker_pool ();
    do_init_tasks ();
  }

  redo_parallel::~redo_parallel()
  {
    assert (queue.get_adding_finished ());
    assert (worker_pool == nullptr);
    assert (waited_for_termination);
  }

  void redo_parallel::set_adding_finished()
  {
    assert(false == waited_for_termination);
    assert(thread_manager != nullptr);
    assert(false == queue.get_adding_finished ());

    queue.set_adding_finished ();
  }

  void redo_parallel::wait_for_termination_and_stop_execution()
  {
    assert (false == waited_for_termination);
    assert (thread_manager != nullptr);

    // busy wait
    while (task_active_state_bookkeeping.any_active ())
      {
	std::this_thread::sleep_for (std::chrono::milliseconds (20));
      }

    worker_pool->stop_execution ();
    thread_manager->destroy_worker_pool (worker_pool);
    assert(worker_pool == nullptr);

    waited_for_termination = true;
  }

  void redo_parallel::do_init_thread_manager()
  {
    assert (thread_manager == nullptr);

    thread_manager = cubthread::get_manager ();
    // NOTE: already called globally (prob. during boot)
    // TODO: how can this be ensured?
    //thread_manager->set_max_thread_count (..);
    //thread_manager->alloc_entries ();
    //thread_manager->init_entries (..);
  }

  void redo_parallel::do_init_worker_pool()
  {
    assert (task_count > 0);
    assert (thread_manager != nullptr);
    assert (worker_pool == nullptr);

    worker_pool = thread_manager->create_worker_pool ( task_count, task_count, "log_recovery_redo_thread_pool",
		  &worker_pool_context_manager, task_count, false /*debug_logging*/);
  }

  void redo_parallel::do_init_tasks()
  {
    assert (task_count > 0);
    assert (worker_pool != nullptr);

    for (unsigned task_idx = 0; task_idx < task_count; ++task_idx)
      {
	// NOTE: task ownership goes to the worker pool
	auto task = new cublogrecovery::redo_task (task_idx, task_active_state_bookkeeping, queue);
	worker_pool->execute (task);
      }
  }

}
