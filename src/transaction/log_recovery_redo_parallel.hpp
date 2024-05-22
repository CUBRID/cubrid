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

#ifndef _LOG_RECOVERY_REDO_PARALLEL_HPP_
#define _LOG_RECOVERY_REDO_PARALLEL_HPP_

#include "log_recovery_redo.hpp"

#include "log_recovery_redo_perf.hpp"

#if defined(SERVER_MODE)
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#endif /* SERVER_MODE */

namespace cublog
{
#if defined(SERVER_MODE)

  /* minimum_log_lsa_monitor - utility to support calculation of a minimum
   *       lsa out of a set of, concurrently (or not), advacing ones; not
   *       all lsa's participate in the game in every scenarios; those that
   *       do not partake are gracefully ignored by being kept to a maximum
   *       sentinel value
   */
  class minimum_log_lsa_monitor final
  {
    private:
      static constexpr int ARRAY_LENGTH = 4;

      using log_lsa_array_t = std::array<log_lsa, ARRAY_LENGTH>;

      enum ARRAY_INDEX
      {
	PRODUCE_IDX = 0,
	CONSUME_IDX = 1,
	IN_PROGRESS_IDX = 2,
	OUTER_IDX = 3,
      };

    public:
      minimum_log_lsa_monitor ();
      minimum_log_lsa_monitor (const minimum_log_lsa_monitor &) = delete;
      minimum_log_lsa_monitor (minimum_log_lsa_monitor &&) = delete;

      minimum_log_lsa_monitor &operator = (const minimum_log_lsa_monitor &) = delete;
      minimum_log_lsa_monitor &operator = (minimum_log_lsa_monitor &&) = delete;

      void set_for_produce (const log_lsa &a_lsa);
      void set_for_produce_and_consume (const log_lsa &a_produce_lsa, const log_lsa &a_consume_lsa);
      void set_for_consume_and_in_progress (const log_lsa &a_consume_lsa, const log_lsa &a_in_progress_lsa);
      void set_for_in_progress (const log_lsa &a_lsa);
      void set_for_outer (const log_lsa &a_lsa);

      /* obtain the current calculated minimum value
       */
      log_lsa get () const;

      /* wait till the recorder minimum lsa has passed the target lsa
       * blocking call
       *
       * return: minimum log_lsa value calculated internally (mainly for testing purpose)
       */
      log_lsa wait_past_target_lsa (const log_lsa &a_target_lsa);

    private:
      void do_set_at (ARRAY_INDEX a_idx, const log_lsa &a_new_lsa);
      template <typename T_LOCKER>
      log_lsa do_locked_get (const T_LOCKER &) const;

    private:
      mutable std::mutex m_values_mtx;
      log_lsa_array_t m_values;

      std::condition_variable m_wait_for_target_value_cv;
  };

  /* a class to handle infrastructure for parallel log recovery/replication RAII-style;
   * usage:
   *  - instantiate an object of this class with the desired number of background workers
   *  - create and add jobs - see 'redo_job_impl'
   *  - after all jobs have been added, explicitly call 'set_adding_finished'
   *  - and, as a final step, call 'wait_for_termination_and_stop_execution'; implementation
   *    explicitly needs these last two steps to be executed in this order to be able to clean-up
   */
  class redo_parallel final
  {
    public:
      /* forward declarations for implementation details */
      class redo_job_base;
      class redo_task;

    public:
      /* - worker_count: the number of parallel tasks to spin that consume jobs
       */
      redo_parallel (unsigned a_worker_count, minimum_log_lsa_monitor *a_minimum_log_lsa);

      redo_parallel (const redo_parallel &) = delete;
      redo_parallel (redo_parallel &&) = delete;

      ~redo_parallel ();

      redo_parallel &operator = (const redo_parallel &) = delete;
      redo_parallel &operator = (redo_parallel &&) = delete;

      /* add new work job
       */
      void add (std::unique_ptr<redo_job_base> &&job);

      /* mandatory to explicitly call this after all data have been added
       */
      void set_adding_finished ();

      /* wait until all data has been consumed internally; blocking call
       */
      void wait_for_idle ();

      /* mandatory to explicitly call this before dtor
       */
      void wait_for_termination_and_stop_execution ();

    private:
      void do_init_worker_pool ();
      void do_init_tasks ();

    private:
      /* rynchronizes prod/cons of log entries in n-prod - m-cons fashion
       * internal implementation detail; not to be used externally
       */
      class redo_job_queue final
      {
	  using ux_redo_job_base = std::unique_ptr<redo_job_base>;
	  using ux_redo_job_deque = std::deque<ux_redo_job_base>;
	  using vpid_set = std::set<VPID>;
	  using log_lsa_set = std::set<log_lsa>;

	public:
	  redo_job_queue (minimum_log_lsa_monitor *a_minimum_log_lsa);
	  ~redo_job_queue ();

	  redo_job_queue (redo_job_queue const &) = delete;
	  redo_job_queue (redo_job_queue &&) = delete;

	  redo_job_queue &operator= (redo_job_queue const &) = delete;
	  redo_job_queue &operator= (redo_job_queue &&) = delete;

	  void push_job (ux_redo_job_base &&job);

	  /* to be called after all known entries have been added
	   * part of a mechanism to signal to the consumers, together with
	   * whether there is still data to process, that they can bail out
	   */
	  void set_adding_finished ();

	  /* mostly for debugging
	   */
	  bool get_adding_finished () const;

	  /* the combination of a null return value with a finished
	   * flag set to true signals to the callers that no more data is expected
	   * and, therefore, they can also terminate
	   */
	  ux_redo_job_base pop_job (bool &adding_finished);

	  void notify_job_finished (const ux_redo_job_base &a_job);

	  /* wait until all data has been consumed internally; blocking call
	   */
	  void wait_for_idle () const;

	private:
	  void assert_idle () const;

	  /* swap internal queues and notify if both are empty
	   * assumes the consume queue is locked
	   */
	  void do_swap_queues_if_needed (const std::lock_guard<std::mutex> &a_consume_lockg);

	  /* find first job that can be consumed (ie: is not already marked
	   * in the 'in progress vpids' set)
	   *
	   * NOTE: '*_locked_*' functions are supposed to be called from within locked
	   * areas with respect to the resources they make use of
	   */
	  ux_redo_job_base do_locked_find_job_to_consume_and_mark_in_progress (
		  const std::lock_guard<std::mutex> &a_consume_lockg,
		  const std::lock_guard<std::mutex> &a_in_progress_lockg);

	  /* NOTE: '*_locked_*' functions are supposed to be called from within locked
	   * areas with respect to the resources they make use of
	   */
	  void do_locked_mark_job_in_progress (
		  const std::lock_guard<std::mutex> &a_in_progress_lockg,
		  const ux_redo_job_base &a_job);

	private:
	  /* two queues are internally managed and take turns at being either
	   * on the producing or on the consumption side
	   */
	  ux_redo_job_deque *m_produce_queue;
	  mutable std::mutex m_produce_queue_mutex;
	  ux_redo_job_deque *m_consume_queue;
	  std::mutex m_consume_queue_mutex;

	  bool m_queues_empty;
	  mutable std::condition_variable m_queues_empty_cv;

	  std::atomic_bool m_adding_finished;

	  /* bookkeeping for log entries currently in process of being applied, this
	   * mechanism guarantees ordering among entries with the same VPID;
	   */
	  vpid_set m_in_progress_vpids;
	  log_lsa_set m_in_progress_lsas;
	  mutable std::mutex m_in_progress_mutex;
	  /* signalled when the 'in progress' containers are empty
	   */
	  mutable std::condition_variable m_in_progress_vpids_empty_cv;

	  /* utility class to maintain a minimum log_lsa that is still
	   * to be processed (consumed); non-owning pointer, can be null
	   */
	  minimum_log_lsa_monitor *m_minimum_log_lsa;
      };

      /* maintain a bookkeeping of tasks that are still performing work;
       * internal implementation detail; not to be used externally
       */
      class task_active_state_bookkeeping final
      {
	public:
	  task_active_state_bookkeeping () = default;

	  task_active_state_bookkeeping (const task_active_state_bookkeeping &) = delete;
	  task_active_state_bookkeeping (task_active_state_bookkeeping &&) = delete;

	  task_active_state_bookkeeping &operator = (const task_active_state_bookkeeping &) = delete;
	  task_active_state_bookkeeping &operator = (task_active_state_bookkeeping &&) = delete;

	  /* increment the internal number of active tasks
	   */
	  inline void set_active ();

	  /* decrement the internal number of active tasks
	   */
	  inline void set_inactive ();

	  /* blocking call until all active tasks terminate
	   */
	  inline void wait_for_termination ();

	private:
	  int m_active_count { 0 };
	  std::mutex m_active_count_mtx;
	  std::condition_variable m_active_count_cv;
      };

    private:
      unsigned m_task_count;

      /* the workpool already has and internal bookkeeping and can also wait for the tasks to terminate;
       * however, it also has a hardcoded maximum wait time (60 seconds) after which it will assert;
       * adding this internal additional bookeeping - which does not assume a maximum wait time - allows
       * to circumvent that hardcoded parameter
       */
      task_active_state_bookkeeping m_task_state_bookkeeping;

      cubthread::entry_workpool *m_worker_pool;

      redo_job_queue m_job_queue;

      bool m_waited_for_termination;
  };


  /* contains a single vpid-backed log record entry to be applied on a page;
  * this is a base class designed to allow handling in a container;
  * internal implementation detail; not to be used externally
  *
  * NOTE: an optimization would be to allow this to be a container of more than
  *  one same-vpid log record entry to be applied in one go by the same thread
  */
  class redo_parallel::redo_job_base
  {
    public:
      redo_job_base (VPID a_vpid, const log_lsa &a_log_lsa)
	: m_vpid (a_vpid), m_log_lsa (a_log_lsa)
      {
	// the logic implemented below, in the queue and task, does allow for null-vpid type of
	// entries to be executed - they are called "to be waited for" or "synched" operations;
	// but, for now, do not allow them to be dispatched to be executed asynchronously
	assert (!VPID_ISNULL (&m_vpid));
      }
      redo_job_base () = delete;
      redo_job_base (redo_job_base const &) = delete;
      redo_job_base (redo_job_base &&) = delete;

      virtual ~redo_job_base () = default;

      redo_job_base &operator = (redo_job_base const &) = delete;
      redo_job_base &operator = (redo_job_base &&) = delete;

      /* log entries come in more than one flavor:
      *  - pertain to a certain vpid - aka: page update
      *  - pertain to a certain VOLID - aka: volume extend, new page
      *  - pertain to no VOLID - aka: database extend, new volume
      */
      inline const VPID &get_vpid () const
      {
	return m_vpid;
      }

      inline const log_lsa &get_log_lsa () const
      {
	return m_log_lsa;
      }

      virtual int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
			   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) = 0;

    private:
      const VPID m_vpid;
      const log_lsa m_log_lsa;
  };


  /* actual job implementation that performs log recovery redo
   */
  template <typename TYPE_LOG_REC>
  class redo_job_impl final : public redo_parallel::redo_job_base
  {
      using log_rec_t = TYPE_LOG_REC;

    public:
      /*
       *  force_each_page_fetch: force fetch log pages each time regardless of other internal
       *                        conditions; needed to be enabled when job is dispatched in
       *                        page server recovery context
       */
      redo_job_impl (VPID a_vpid, const log_lsa &a_rcv_lsa, const log_lsa *a_end_redo_lsa,
		     LOG_RECTYPE a_log_rtype, bool force_each_page_fetch);

      redo_job_impl (redo_job_impl const &) = delete;
      redo_job_impl (redo_job_impl &&) = delete;

      ~redo_job_impl () override = default;

      redo_job_impl &operator = (redo_job_impl const &) = delete;
      redo_job_impl &operator = (redo_job_impl &&) = delete;

      int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
		   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) override;

    private:
      const log_lsa *m_end_redo_lsa;  // by design pointer is guaranteed to outlive this instance
      const LOG_RECTYPE m_log_rtype;
      const log_reader::fetch_mode m_log_reader_page_fetch_mode;
  };

  /*********************************************************************
   * template/inline implementations
   *********************************************************************/

  template <typename TYPE_LOG_REC>
  redo_job_impl<TYPE_LOG_REC>::redo_job_impl (VPID a_vpid, const log_lsa &a_rcv_lsa, const log_lsa *a_end_redo_lsa,
      LOG_RECTYPE a_log_rtype, bool force_each_page_fetch)
    : redo_parallel::redo_job_base (a_vpid, a_rcv_lsa)
    , m_end_redo_lsa (a_end_redo_lsa)
    , m_log_rtype (a_log_rtype)
    , m_log_reader_page_fetch_mode (force_each_page_fetch
				    ? log_reader::fetch_mode::FORCE
				    : log_reader::fetch_mode::NORMAL)
  {
  }

  template <typename TYPE_LOG_REC>
  int  redo_job_impl<TYPE_LOG_REC>::execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
      LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support)
  {
    const auto &rcv_lsa = get_log_lsa ();
    const int err_set_lsa_and_fetch_page
      = log_pgptr_reader.set_lsa_and_fetch_page (rcv_lsa, m_log_reader_page_fetch_mode);
    if (err_set_lsa_and_fetch_page != NO_ERROR)
      {
	return err_set_lsa_and_fetch_page;
      }
    log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
    log_pgptr_reader.advance_when_does_not_fit (sizeof (log_rec_t));
    const log_rec_t log_rec
      = log_pgptr_reader.reinterpret_copy_and_add_align<log_rec_t> ();

    const auto &rcv_vpid = get_vpid ();
    log_rv_redo_record_sync<log_rec_t> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, m_end_redo_lsa,
					m_log_rtype, undo_unzip_support, redo_unzip_support);
    return NO_ERROR;
  }

#else /* SERVER_MODE */
  /* dummy implementations for SA mode
   */
  class minimum_log_lsa_monitor final { };
  class redo_parallel final { };
#endif /* SERVER_MODE */
}


/*
 * log_rv_redo_record_sync_or_dispatch_async - execute a redo record synchronously or
 *                    (in future) asynchronously
 *
 * return: nothing
 *
 *   thread_p(in):
 *   log_pgptr_reader(in/out): log reader
 *   log_rec(in): log record structure with info about the location and size of the data in the log page
 *   rcv_lsa(in): Reset data page (rcv->pgptr) to this LSA
 *   log_rtype(in): log record type needed to check if diff information is needed or should be skipped
 *   undo_unzip_support(out): extracted undo data support structure (set as a side effect)
 *   redo_unzip_support(out): extracted redo data support structure (set as a side effect); required to
 *                    be passed by address because it also functions as an internal working buffer;
 *                    functions as an outside managed (ie: longer lived) buffer space for the underlying
 *                    logic to perform its work; the pointer to the buffer is then passed - non-owningly
 *                    - to the rcv structure which, in turn, is passed to the actual apply function
 *   force_each_log_page_fetch(in): the log page will be fetched anew each time a fetch is requested
 *                    regardless of other considerations
 *
 */
template <typename T>
void
log_rv_redo_record_sync_or_dispatch_async (
	THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
	const T &log_rec, const log_lsa &rcv_lsa,
	const LOG_LSA *end_redo_lsa, LOG_RECTYPE log_rtype,
	LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support,
	std::unique_ptr<cublog::redo_parallel> &parallel_recovery_redo,
	bool force_each_log_page_fetch, log_recovery_redo_perf_stat &a_rcv_redo_perf_stat)
{
  const VPID rcv_vpid = log_rv_get_log_rec_vpid<T> (log_rec);
  // at this point, vpid can either be valid or not

#if defined(SERVER_MODE)
  const LOG_DATA &log_data = log_rv_get_log_rec_data<T> (log_rec);
  const bool need_sync_redo = log_rv_need_sync_redo (rcv_vpid, log_data.rcvindex);
  a_rcv_redo_perf_stat.time_and_increment (PERF_STAT_ID_REDO_OR_PUSH_PREP);

  // once vpid is extracted (or not), and depending on parameters, either dispatch the applying of
  // log redo asynchronously, or invoke synchronously
  if (parallel_recovery_redo == nullptr || need_sync_redo)
    {
      // invoke sync
      log_rv_redo_record_sync<T> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, end_redo_lsa, log_rtype,
				  undo_unzip_support, redo_unzip_support);
      a_rcv_redo_perf_stat.time_and_increment (PERF_STAT_ID_REDO_OR_PUSH_DO_SYNC);
    }
  else
    {
      // dispatch async
      using redo_job_impl_t = cublog::redo_job_impl<T>;
      std::unique_ptr<redo_job_impl_t> job
      {
	new redo_job_impl_t (rcv_vpid, rcv_lsa, end_redo_lsa, log_rtype, force_each_log_page_fetch)
      };
      parallel_recovery_redo->add (std::move (job));
      a_rcv_redo_perf_stat.time_and_increment (PERF_STAT_ID_REDO_OR_PUSH_DO_ASYNC);
    }
#else // !SERVER_MODE = SA_MODE
  log_rv_redo_record_sync<T> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, end_redo_lsa, log_rtype,
			      undo_unzip_support, redo_unzip_support);
#endif // !SERVER_MODE = SA_MODE
}

#endif // _LOG_RECOVERY_REDO_PARALLEL_HPP_
