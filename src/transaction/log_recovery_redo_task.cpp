
#include "log_recovery_redo.hpp"
#include "log_impl.h"

#include <iomanip>
#include <sstream>

namespace log_recovery_ns
{
  constexpr unsigned short redo_task::WAIT_AND_CHECK_MILLIS;

  redo_task::redo_task (std::size_t _task_id
			, redo_task_active_state_bookkeeping &_task_active_state_bookkeeping
			, redo_log_rec_entry_queue &_bucket_queue)
    : task_id (_task_id), task_active_state_bookkeeping (_task_active_state_bookkeeping), bucket_queue (_bucket_queue)
  {
    // important to set this at this moment and not when execution begins
    // to circumvent race conditions where all tasks haven't yet started work
    // while already bookkeeping is being checked
    task_active_state_bookkeeping.set_active (task_id);

    log_zip_realloc_if_needed (undo_unzip_support, LOGAREA_SIZE);
    log_zip_realloc_if_needed (redo_unzip_support, LOGAREA_SIZE);
  }

  void redo_task::execute (context_type &context)
  {
    bool finished = false;

    for (; !finished ;)
      {
	bool buckets_adding_finished = false;
	auto bucket = bucket_queue.locked_pop (buckets_adding_finished);

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

		bucket_queue.notify_in_progress_vpid_finished (bucket_vpid);

		if (bucket_is_to_be_waited_for)
		  {
		    bucket_queue.notify_to_be_waited_for_op_finished();
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

}
