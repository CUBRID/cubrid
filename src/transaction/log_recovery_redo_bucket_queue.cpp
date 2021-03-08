
#include "dbtype_def.hpp"
#include "log_recovery_redo.hpp"

namespace log_recovery_ns
{
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
}
