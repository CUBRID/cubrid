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
#include "log_replication.hpp"

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
      redo_parallel (unsigned a_worker_count);

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

      /* check if all fed data has ben consumed internally; non-blocking call
       * NOTE: the nature of this function is 'volatile' - ie: what might be
       * true at the moment the function is called is not necessarily true a moment
       * later; it can be useful only if the caller is aware of the execution context
       */
      bool is_idle () const;

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

	public:
	  redo_job_queue ();
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

	  /* check if all fed data has ben consumed internally; non-blocking call
	   * NOTE: the nature of this function is 'volatile' - ie: what might be
	   * true at the moment the function is called is not necessarily true a moment
	   * later; it can be useful only if the caller is aware of the execution context
	   */
	  bool is_idle () const;

	private:
	  void assert_idle () const;

	  /* swap internal queues and notify if both are empty
	   * assumes the consume queue is locked
	   */
	  void do_swap_queues_if_needed ();

	  /* find first job that can be consumed (ie: is not already marked
	   * in the 'in progress vpids' set)
	   *
	   * NOTE: '*_locked_*' functions are supposed to be called from within locked
	   * areas with respect to the resources they make use of
	   */
	  ux_redo_job_base do_locked_find_job_to_consume ();

	  /* NOTE: '*_locked_*' functions are supposed to be called from within locked
	   * areas with respect to the resources they make use of
	   */
	  void do_locked_mark_job_started (const ux_redo_job_base &a_job);

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
	  mutable std::mutex m_in_progress_mutex;
	  mutable std::condition_variable m_in_progress_vpids_empty_cv;
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


  /* actual job implementation that performs log recovery/replication redo,
   * also used for log replication
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


  /* job implementation that performs log replication delay calculation
   * using log records that register creation time
   */
  template <typename TYPE_LOG_REC> // TODO: does not need to be a template
  class redo_job_replication_delay_impl final : public redo_parallel::redo_job_base
  {
      using log_rec_t = TYPE_LOG_REC;

      /* sentinel VPID value needed for the internal mechanics of the parallel log recovery/replication
       * internally, such a VPID is needed to maintain absolute order of the processing
       * of the log records with respect to their order in the global log record
       */
      static constexpr short SENTINEL_VOLID = -2;
      static constexpr int32_t SENTINEL_PAGEID = -2;
      static constexpr struct vpid SENTINEL_VPID = { SENTINEL_PAGEID, SENTINEL_VOLID };

    public:
      redo_job_replication_delay_impl (const log_lsa &a_rcv_lsa, time_t a_start_time_msec, const char *a_source);

      redo_job_replication_delay_impl (redo_job_replication_delay_impl const &) = delete;
      redo_job_replication_delay_impl (redo_job_replication_delay_impl &&) = delete;

      ~redo_job_replication_delay_impl () override = default;

      redo_job_replication_delay_impl &operator = (redo_job_replication_delay_impl const &) = delete;
      redo_job_replication_delay_impl &operator = (redo_job_replication_delay_impl &&) = delete;

      int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
		   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) override;

    private:
      const time_t m_start_time_msec;
      const char *const m_source;
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

  template <typename TYPE_LOG_REC>
  constexpr short redo_job_replication_delay_impl<TYPE_LOG_REC>::SENTINEL_VOLID;
  template <typename TYPE_LOG_REC>
  constexpr int32_t redo_job_replication_delay_impl<TYPE_LOG_REC>::SENTINEL_PAGEID;
  template <typename TYPE_LOG_REC>
  constexpr struct vpid redo_job_replication_delay_impl<TYPE_LOG_REC>::SENTINEL_VPID;

  template <typename TYPE_LOG_REC>
  redo_job_replication_delay_impl<TYPE_LOG_REC>::redo_job_replication_delay_impl (
	  const log_lsa &a_rcv_lsa, time_t a_start_time_msec, const char *a_source)
    : redo_parallel::redo_job_base (SENTINEL_VPID, a_rcv_lsa)
    , m_start_time_msec (a_start_time_msec)
    , m_source (a_source)
  {
  }

  template <typename TYPE_LOG_REC>
  int  redo_job_replication_delay_impl<TYPE_LOG_REC>::execute (THREAD_ENTRY *thread_p, log_reader &,
      LOG_ZIP &, LOG_ZIP &)
  {
    const int res = log_rpl_calculate_replication_delay (thread_p, m_start_time_msec, m_source);
    return res;
  }

#else /* SERVER_MODE */
  /* dummy implementation for SA mode
   */
  class redo_parallel final
  {
  };
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
	bool force_each_log_page_fetch)
{
  const VPID rcv_vpid = log_rv_get_log_rec_vpid<T> (log_rec);
  // at this point, vpid can either be valid or not

#if defined(SERVER_MODE)
  const LOG_DATA &log_data = log_rv_get_log_rec_data<T> (log_rec);
  const bool need_sync_redo = log_rv_need_sync_redo (rcv_vpid, log_data.rcvindex);
  assert (log_data.rcvindex != RVDK_UNRESERVE_SECTORS || need_sync_redo);

  // once vpid is extracted (or not), and depending on parameters, either dispatch the applying of
  // log redo asynchronously, or invoke synchronously
  if (parallel_recovery_redo == nullptr || need_sync_redo)
    {
      // To apply RVDK_UNRESERVE_SECTORS, one must first wait for all changes in this sector to be redone.
      // Otherwise, asynchronous jobs skip redoing changes in this sector's pages because they are seen as
      // deallocated. When the same sector is reserved again, redo is resumed in the sector's pages, but
      // the pages are not in a consistent state. The current workaround is to wait for all changes to be
      // finished, including changes in the unreserved sector.
      if (parallel_recovery_redo != nullptr && log_data.rcvindex == RVDK_UNRESERVE_SECTORS)
	{
	  parallel_recovery_redo->wait_for_idle ();
	}
#endif

      // invoke sync
      log_rv_redo_record_sync<T> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, end_redo_lsa, log_rtype,
				  undo_unzip_support, redo_unzip_support);
#if defined(SERVER_MODE)
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
    }
#endif
}

#endif // _LOG_RECOVERY_REDO_PARALLEL_HPP_
