
#include "log_recovery_redo.hpp"
//#include "log_recovery_redo_debug_helpers.hpp"
#include "storage_common.h"
#include "thread_compat.hpp"
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#include "thread_task.hpp"
#include "thread_worker_pool.hpp"

#include <iomanip>
#include <iostream>

struct log_recovery_test_config
{
  size_t max_volume_count_per_database;
  size_t max_page_count_per_volume;

  /* one of the concurrent transactions is supposed to generate log entries in the same VPID
   */
  //size_t max_concurrent_transaction_count;

  /* one transaction will generate log entries in one VPID; this is the
   * max number of entries that a transaction will produce
   */
  //size_t max_log_entry_count_per_transaction;

  size_t task_count;
  size_t log_entry_count;

  bool dump_prod_cons_data;
};

int execute_test (cubthread::manager *_cub_thread_manager, const log_recovery_test_config &_test_config)
{
#if (0)
  //
  // thread pool
  //
  const auto core_count = static_cast<std::size_t> (std::thread::hardware_concurrency());
  cubthread::entry_manager dispatching_thread_pool_manager;
  cubthread::entry_workpool *dispatching_worker_pool =
	  _cub_thread_manager->create_worker_pool (
		  _test_config.task_count, _test_config.task_count, nullptr, &dispatching_thread_pool_manager,
		  core_count, false /*debug_logging*/);

  log_recovery_ns::redo_log_rec_entry_queue bucket_queue;

  ux_db_database db_online { new db_database (_test_config.max_volume_count_per_database, _test_config.max_page_count_per_volume) };
  db_online->initialize();

  ux_db_database db_recovery { new db_database (_test_config.max_volume_count_per_database, _test_config.max_page_count_per_volume) };
  db_recovery->initialize();

  //
  // launch log processing tasks
  //
  log_recovery_ns::redo_task_active_state_bookkeeping task_active_state_bookkeeping;

  consumption_accumulator dbg_accumulator;

  for (decltype (_test_config.task_count) task_idx = 0; task_idx < _test_config.task_count; ++task_idx)
    {
      // NOTE: task ownership goes to the worker pool
      auto task = new log_recovery_ns::redo_task (task_idx, task_active_state_bookkeeping, bucket_queue, dbg_accumulator,
	  db_recovery);
      dispatching_worker_pool->execute (task);
    }

  //
  // produce data in the main thread
  //
  db_global_values global_values;

  for (decltype (_test_config.log_entry_count) idx = 0; idx < _test_config.log_entry_count; ++idx)
    {
      auto log_entry = db_online->generate_changes (global_values);

      if (_test_config.dump_prod_cons_data)
	{
	  std::cout << "P: "
		    << "syn_" << ((log_entry->get_is_to_be_waited_for_op ()) ? '1' : '0')
		    << std::setw (4) << log_entry->get_vpid().volid << std::setfill ('_')
		    << std::setw (5) << log_entry->get_vpid().pageid << std::setfill (' ')
		    << "  eids: " << std::setw (7) << log_entry->get_entry_id()
		    << std::endl;
	}

//      log_recovery_ns::ux_redo_entry_bucket bucket
//      {
//	new  log_recovery_ns::redo_entry_bucket (
//	VPID { log_entry->get_vpid().pageid, log_entry->get_vpid().volid }
//	, log_entry->is_to_be_waited_for())
//      };
//      bucket->add_entry (std::move (log_entry));

//      bucket_queue.locked_push_bucket (std::move (bucket));
      bucket_queue.locked_push (std::move (log_entry));

      // TODO: a mechanism for the producer to not overload the queue, if needed
      //std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

  bucket_queue.set_adding_finished();

  //
  // wait consumption to end
  //
  while (task_active_state_bookkeeping.any_active())
    {
      std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }

  dispatching_worker_pool->stop_execution ();
  _cub_thread_manager->destroy_worker_pool (dispatching_worker_pool);

  //
  // output consumption details
  //
  if (_test_config.dump_prod_cons_data)
    {
      std::cout << std::endl;
      for (const auto &acc: dbg_accumulator.get_data())
	{
	  std::cout << acc;
	}
    }

  std::cout << "BucketSkips = " << bucket_queue.get_dbg_stats_cons_queue_skip_count()
	    << "  SpinWaits = " << bucket_queue.get_stats_spin_wait_count()
	    << std::endl;
  if (*db_online == *db_recovery)
    {
      std::cout << "Databases are equal" << std::endl;
      return 0;
    }
  else
    {
      std::cout << "Databases are NOT equal" << std::endl;
      return -1;
    }
#else
  std::cout << "ERROR: test is skipped" << std::endl;
  return -1;
#endif
}

int main ()
{
  srand (time (nullptr));

  cubthread::entry *thread_pool = nullptr;
  cubthread::initialize (thread_pool);

  cubthread::manager *cub_thread_manager;
  cub_thread_manager = cubthread::get_manager ();
  cub_thread_manager->set_max_thread_count (100);
  cub_thread_manager->alloc_entries ();
  cub_thread_manager->init_entries (false);

  std::array<size_t, 2> volumes_per_database_arr { 2u, 10u };
  std::array<size_t, 3> pages_per_volume_arr { 10u, 1000u, 1024u * 16u };
  std::array<size_t, 2> log_entry_count_arr { 32u * 1024u, 4u * 32u * 1024u };
  for (const size_t volumes_per_database : volumes_per_database_arr)
    for (const size_t pages_per_volume : pages_per_volume_arr)
      for (const size_t log_entry_count : log_entry_count_arr)
	{
	  const log_recovery_test_config test_config =
	  {
	    volumes_per_database, // max_volume_count_per_database
	    pages_per_volume, // max_page_count_per_volume
	    std::thread::hardware_concurrency(), // task_count
	    log_entry_count, // log_entry_count
	    false // dump_prod_cons_data
	  };

	  std::cout << "TEST:"
		    << "\t volumes=" << test_config.max_volume_count_per_database
		    << "\t pages=" << test_config.max_page_count_per_volume
		    << "\t tasks=" << test_config.task_count
		    << "\t entries=" << test_config.log_entry_count
		    << std::endl;

	  for (int i = 0; i < 3; ++i)
	    {
	      std::cout << "\t#" << (i + 1) << std::endl;
	      execute_test (cub_thread_manager, test_config);
	    }
	}

  return 0;
}
