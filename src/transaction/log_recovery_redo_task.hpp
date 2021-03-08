#ifndef LOG_RECOVERY_REDO_TASK_HPP
#define LOG_RECOVERY_REDO_TASK_HPP

#include "log_recovery_redo.hpp"
#include "log_recovery_redo_debug_helpers.hpp"
#include "thread_entry.hpp"
#include "thread_task.hpp"

#include <bitset>
#include <iomanip>
#include <sstream>

namespace log_recovery_ns
{

  class redo_task_active_state_bookkeeping
  {
    public:
      static constexpr std::size_t BOOKKEEPING_MAX_COUNT = 1024;

    public:
      redo_task_active_state_bookkeeping()
      {
        active_set.reset();
      }

      void set_active (std::size_t _id)
      {
        std::lock_guard<std::mutex> lck (active_set_mutex);

	assert (_id < BOOKKEEPING_MAX_COUNT);
	active_set.set (_id);
      }

      void set_inactive (std::size_t _id)
      {
        std::lock_guard<std::mutex> lck (active_set_mutex);

	assert (_id < BOOKKEEPING_MAX_COUNT);
	active_set.reset (_id);
      }

      bool any_active() const
      {
        std::lock_guard<std::mutex> lck (active_set_mutex);

        return active_set.any();
      }

    private:
      std::bitset<BOOKKEEPING_MAX_COUNT> active_set;
      mutable std::mutex active_set_mutex;
  };

  class redo_task : public cubthread::task<cubthread::entry>
  {
    public:
      static constexpr unsigned short WAIT_AND_CHECK_MILLIS = 5;

    public:
      // TODO: ctor with task_id

      ~redo_task() override
      {
        const auto i = 0;
      }

      redo_task (std::size_t _task_id
                 , redo_task_active_state_bookkeeping &_task_active_state_bookkeeping
                 , redo_log_rec_entry_queue &_bucket_queue
                 , consumption_accumulator &_dbg_accumulator
                 , ux_db_database &_dbg_db_database)
        : task_id (_task_id), task_active_state_bookkeeping (_task_active_state_bookkeeping), bucket_queue (_bucket_queue)
        , dbg_accumulator (_dbg_accumulator), dbg_db_database (_dbg_db_database)
      {
        // important to set this at this moment and not when execution begins
        // to circumvent race conditions where all tasks haven't yet started work
        // while already bookkeeping is being checked
        task_active_state_bookkeeping.set_active (task_id);
      }

      void execute (context_type &context)
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
		    dummy_busy_wait (bucket->get_millis ());

		    // save needed data before move
		    const VPID bucket_vpid = bucket->get_vpid ();
		    const bool bucket_is_to_be_waited_for = bucket->get_is_to_be_waited_for_op();

		    dbg_db_database->apply_changes (std::move (bucket));

		    dbg_ss_consume << std::endl;
		    dbg_accumulator.accumulate (dbg_ss_consume.str());

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

    private:
      static void dummy_busy_wait (size_t _millis)
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

    private:
      std::size_t task_id;
      redo_task_active_state_bookkeeping &task_active_state_bookkeeping;

      redo_log_rec_entry_queue &bucket_queue;

      consumption_accumulator &dbg_accumulator;
      ux_db_database &dbg_db_database;
  };

  constexpr unsigned short redo_task::WAIT_AND_CHECK_MILLIS;

}

#endif // LOG_RECOVERY_REDO_TASK_HPP
