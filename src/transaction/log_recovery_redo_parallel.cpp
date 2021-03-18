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

#include "vpid.hpp"
#include "log_recovery_redo_parallel.hpp"

namespace cublog
{
  /**********************
   * redo_parallel::redo_job_queue
   **********************/

  redo_parallel::redo_job_queue::redo_job_queue ()
    : produce_queue ( new ux_redo_job_deque () )
    , consume_queue ( new ux_redo_job_deque () )
    , queues_empty (false)
    , adding_finished { false }
    , to_be_waited_for_op_in_progress (false)
  {
  }

  redo_parallel::redo_job_queue::~redo_job_queue ()
  {
    assert (produce_queue->size () == 0);
    assert (consume_queue->size () == 0);
    assert (in_progress_vpids.size () == 0);

    delete produce_queue;
    produce_queue = nullptr;

    delete consume_queue;
    consume_queue = nullptr;
  }

  void redo_parallel::redo_job_queue::locked_push (ux_redo_job_base &&job)
  {
    std::lock_guard<std::mutex> lck (produce_queue_mutex);
    produce_queue->push_back (std::move (job));
    queues_empty = false;
  }

  void redo_parallel::redo_job_queue::set_adding_finished ()
  {
    adding_finished.store (true);
  }

  bool redo_parallel::redo_job_queue::get_adding_finished () const
  {
    return adding_finished.load ();
  }

  redo_parallel::redo_job_queue::ux_redo_job_base redo_parallel::redo_job_queue::locked_pop (bool &out_adding_finished)
  {
    std::unique_lock<std::mutex> consume_queue_lock (consume_queue_mutex);
    // stop at the barrier if an operation which needs to be waited for is in progress
    to_be_waited_for_op_in_progress_cv.wait (consume_queue_lock, [this] ()
    {
      return !to_be_waited_for_op_in_progress;
    });

    out_adding_finished = false;

    // if consumption of everything in consume queue finished; see whether there's some more
    if (consume_queue->size () == 0)
      {
	// consumer queue is locked anyway, interested in not locking the producer queue
	bool notify_queues_empty = false;
	{
	  std::lock_guard<std::mutex> lck (produce_queue_mutex);
	  //consume_queue = ATOMIC_TAS_ADDR (&produce_queue, consume_queue);
	  std::swap (produce_queue, consume_queue);
	  queues_empty = produce_queue->empty () && consume_queue->empty ();
	  notify_queues_empty = queues_empty;
	}
	if (notify_queues_empty)
	  {
	    queues_empty_cv.notify_one ();
	  }

	// TODO: a second barrier such that, consumption threads do not 'busy wait' by
	// constantly swapping two empty containers; works in combination with a notification
	// dispatched after new work items are added to the produce queue; this, in effect means that
	// this function will semantically become 'blocking_pop'
      }

    if (consume_queue->size () > 0)
      {
	ux_redo_job_base first_in_consume_queue;

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
	  auto consume_queue_it = consume_queue->begin ();
	  for (; consume_queue_it != consume_queue->end (); ++consume_queue_it)
	    {
	      const VPID it_vpid = (*consume_queue_it)->get_vpid ();
	      if (in_progress_vpids.find ((it_vpid)) == in_progress_vpids.cend ())
		{
		  break;
		}
	    }

	  if (consume_queue_it != consume_queue->end ())
	    {
	      first_in_consume_queue = std::move (*consume_queue_it);
	      consume_queue->erase (consume_queue_it);
	    }
	  else
	    {
	      // consumer will have to spin-wait
	      return nullptr;
	    }

	  // IDEA:
	  // if this is a  non-synched op, search all other entries from the current queue
	  // pertaining to the same vpid and consolidate them all in a single 'execution'; stop
	  // when all entries in the consume queue have been added or when a synched (to-be-waited-for)
	  // entry is found

	  const VPID vpid_to_be_processed = first_in_consume_queue->get_vpid ();
	  assert (in_progress_vpids.find (vpid_to_be_processed) == in_progress_vpids.cend ());
	  in_progress_vpids.insert (vpid_to_be_processed);
	} // unlock in_progress_vpids

	/* unlocking before this will not guarantee total ordering amongst entries' execution
	 */
	//consume_queue_lock.unlock();

	if (first_in_consume_queue->get_is_to_be_waited_for ())
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
	out_adding_finished = adding_finished.load ();

	// if no more data will be produced (signalled by the flag), the
	// consumer will just need to terminate; otherwise, consumer is expected to
	// spin-wait and try again
	return nullptr;
      }
  }

  void redo_parallel::redo_job_queue::notify_to_be_waited_for_op_finished ()
  {
    assert (to_be_waited_for_op_in_progress == true);
    to_be_waited_for_op_in_progress = false;
    // notify all waiting threads, as there can be many which can pick-up work
    to_be_waited_for_op_in_progress_cv.notify_all ();
  }

  void redo_parallel::redo_job_queue::notify_in_progress_vpid_finished (VPID _vpid)
  {
    bool set_empty = false;
    {
      std::lock_guard<std::mutex> lock_in_progress_vpids (in_progress_vpids_mutex);
      assert (in_progress_vpids.find (_vpid) != in_progress_vpids.cend ());
      in_progress_vpids.erase (_vpid);
      set_empty = in_progress_vpids.empty ();
    }
    if (set_empty)
      {
	in_progress_vpids_empty_cv.notify_one ();
      }
  }

  void redo_parallel::redo_job_queue::wait_for_idle ()
  {
    {
      std::unique_lock<std::mutex> empty_queues_lock (produce_queue_mutex);
      queues_empty_cv.wait (empty_queues_lock, [this] ()
      {
	return queues_empty;
      });
    }

    {
      std::unique_lock<std::mutex> empty_in_progress_vpids_lock (in_progress_vpids_mutex);
      in_progress_vpids_empty_cv.wait (empty_in_progress_vpids_lock, [this] ()
      {
	return in_progress_vpids.empty ();
      });
    }
  }

  /**********************
   * redo_parallel::redo_task
   **********************/

  /* a long running task looping and processing redo log jobs;
   * offers some 'support' instances to the passing guest log jobs: a log reader,
   * unzip support memory for undo data, unzip support memory for redo data;
   * internal implementation detail; not to be used externally
   */
  class redo_parallel::redo_task final : public cubthread::task<cubthread::entry>
  {
    public:
      static constexpr unsigned short WAIT_AND_CHECK_MILLIS = 5;

    public:
      redo_task (redo_parallel::redo_job_queue &a_queue);
      redo_task (const redo_task &) = delete;
      redo_task (redo_task &&) = delete;

      redo_task &operator = (const redo_task &) = delete;
      redo_task &operator = (redo_task &&) = delete;

      ~redo_task () override;

      void execute (context_type &context);

    private:
      redo_parallel::redo_job_queue &queue;

      log_reader log_pgptr_reader;
      LOG_ZIP undo_unzip_support;
      LOG_ZIP redo_unzip_support;
  };

  constexpr unsigned short redo_parallel::redo_task::WAIT_AND_CHECK_MILLIS;

  redo_parallel::redo_task::redo_task (redo_job_queue &a_queue)
    : queue (a_queue)
  {
    log_zip_realloc_if_needed (undo_unzip_support, LOGAREA_SIZE);
    log_zip_realloc_if_needed (redo_unzip_support, LOGAREA_SIZE);
  }

  redo_parallel::redo_task::~redo_task ()
  {
    log_zip_free_data (undo_unzip_support);
    log_zip_free_data (redo_unzip_support);
  }

  void redo_parallel::redo_task::execute (context_type &context)
  {
    bool finished = false;

    //
    context.tran_index = LOG_SYSTEM_TRAN_INDEX;

    for (; !finished ;)
      {
	bool adding_finished = false;
	std::unique_ptr<redo_job_base> job = queue.locked_pop (adding_finished);

	if (job == nullptr && adding_finished)
	  {
	    finished = true;
	  }
	else
	  {
	    if (job == nullptr)
	      {
		// TODO: if needed, check if requested to finish ourselves

		// expecting more data, sleep and check again
		std::this_thread::sleep_for (std::chrono::milliseconds (WAIT_AND_CHECK_MILLIS));
	      }
	    else
	      {
		THREAD_ENTRY *const thread_entry = &context;
		job->execute (thread_entry, log_pgptr_reader, undo_unzip_support, redo_unzip_support);

		queue.notify_in_progress_vpid_finished (job->get_vpid ());

		if (job->get_is_to_be_waited_for ())
		  {
		    queue.notify_to_be_waited_for_op_finished ();
		  }
	      }
	  }
      }
  }

  /**********************
   * redo_parallel
   **********************/

  redo_parallel::redo_parallel (int a_worker_count)
    : thread_manager (nullptr), worker_pool (nullptr), waited_for_termination (false)
  {
    assert (a_worker_count > 0);
    if (a_worker_count <= 0)
      {
	a_worker_count = std::thread::hardware_concurrency ();
      }
    task_count = static_cast<unsigned> (a_worker_count);

    do_init_thread_manager ();
    do_init_worker_pool ();
    do_init_tasks ();
  }

  redo_parallel::~redo_parallel ()
  {
    assert (queue.get_adding_finished ());
    assert (worker_pool == nullptr);
    assert (waited_for_termination);
  }

  void redo_parallel::add (std::unique_ptr<redo_job_base> &&job)
  {
    assert (false == waited_for_termination);
    assert (thread_manager != nullptr);
    assert (false == queue.get_adding_finished ());

    queue.locked_push (std::move (job));
  }

  void redo_parallel::set_adding_finished ()
  {
    assert (false == waited_for_termination);
    assert (thread_manager != nullptr);
    assert (false == queue.get_adding_finished ());

    queue.set_adding_finished ();
  }

  void redo_parallel::wait_for_termination_and_stop_execution ()
  {
    assert (false == waited_for_termination);
    assert (thread_manager != nullptr);

    worker_pool->stop_execution ();
    thread_manager->destroy_worker_pool (worker_pool);
    assert (worker_pool == nullptr);

    waited_for_termination = true;
  }

  void redo_parallel::wait_for_idle ()
  {
    assert (false == waited_for_termination);
    assert (thread_manager != nullptr);
    assert (false == queue.get_adding_finished ());

    queue.wait_for_idle ();
  }

  void redo_parallel::do_init_thread_manager ()
  {
    assert (thread_manager == nullptr);

    // NOTE: already initialized globally (probably during boot)
    thread_manager = cubthread::get_manager ();
  }

  void redo_parallel::do_init_worker_pool ()
  {
    assert (task_count > 0);
    assert (thread_manager != nullptr);
    assert (worker_pool == nullptr);

    worker_pool = thread_manager->create_worker_pool ( task_count, task_count, "log_recovery_redo_thread_pool",
		  &worker_pool_context_manager, task_count, false /*debug_logging*/);
  }

  void redo_parallel::do_init_tasks ()
  {
    assert (task_count > 0);
    assert (worker_pool != nullptr);

    for (unsigned task_idx = 0; task_idx < task_count; ++task_idx)
      {
	// NOTE: task ownership goes to the worker pool
	auto task = new redo_task (queue);
	worker_pool->execute (task);
      }
  }

}
