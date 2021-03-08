#ifndef LOG_RECOVERY_REDO_HPP
#define LOG_RECOVERY_REDO_HPP

#include "storage_common.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace log_recovery_ns
{
  /* contains a single vpid-backed log record entry to be applied on a page
   *
   * NOTE: an optimization would be to allow this to be a container of more than
   *  one same-vpid log record entry to be applied in one go by the same thread
   */
  class redo_log_rec_entry
  {
    public:
      redo_log_rec_entry () = delete;
      redo_log_rec_entry ( redo_log_rec_entry const &) = delete;
      redo_log_rec_entry ( redo_log_rec_entry &&) = delete;

      redo_log_rec_entry &operator = ( redo_log_rec_entry const &) = delete;
      redo_log_rec_entry &operator = ( redo_log_rec_entry &&) = delete;

      redo_log_rec_entry (INT64 _entry_id, VPID _vpid, size_t _millis)
        : entry_id (_entry_id), vpid (_vpid), millis (_millis)
      {
      }

      /* uniquely identifies a log entry
       */
      INT64 get_entry_id() const
      {
        return entry_id;
      }

      /* log entries come in more than one flavor:
       *  - pertain to a certain vpid - aka: page update
       *  - pertain to a certain VOLID - aka: volume extend, new page
       *  - pertain to no VOLID - aka: database extend, new volume
       */
      const VPID &get_vpid() const
      {
        return vpid;
      }

      inline VOLID get_vol_id() const
      {
        return vpid.volid;
      }

      inline PAGEID get_page_id() const
      {
        return vpid.pageid;
      }

      /* NOTE: functions are needed only for testing purposes and might not have
       * any usefulness/meaning upon integration in production code
       */
      bool get_is_to_be_waited_for_op() const
      {
        return vpid.volid == NULL_VOLID || vpid.pageid == NULL_PAGEID;
      }

      bool is_volume_creation() const
      {
        return vpid.volid == NULL_VOLID && vpid.pageid == NULL_PAGEID;
      }
      bool is_volume_extension() const
      {
        return vpid.volid != NULL_VOLID && vpid.pageid == NULL_PAGEID;
      }
      bool is_page_modification() const
      {
        return vpid.volid != NULL_VOLID && vpid.pageid != NULL_PAGEID;
      }

      bool operator == (const redo_log_rec_entry &_that) const
      {
        const bool res = entry_id == _that.entry_id
                         && get_vol_id() == _that.get_vol_id()
                         && get_page_id() == _that.get_page_id()
                         && get_millis() == _that.get_millis();
        if (!res)
          {
            const auto dummy = res;
          }
        return res;
      }

      /* NOTE: actual payload goes below
       */

      size_t get_millis() const
      {
        return millis;
      }

    private:
      INT64 entry_id;
      VPID vpid;

      /* NOTE: actual payload goes below
       */
      size_t millis;
  };
  using ux_redo_lsa_log_entry = std::unique_ptr<redo_log_rec_entry>;


  /* rynchronizes prod/cons of log entries in n-prod - m-cons fashion
   */
  class redo_log_rec_entry_queue
  {
      using ux_entry_deque = std::deque<ux_redo_lsa_log_entry>;
      using vpid_set = std::set<VPID>;

    public:
      redo_log_rec_entry_queue();
      ~redo_log_rec_entry_queue();

      redo_log_rec_entry_queue ( redo_log_rec_entry_queue const & ) = delete;
      redo_log_rec_entry_queue ( redo_log_rec_entry_queue && ) = delete;
      redo_log_rec_entry_queue &operator= ( redo_log_rec_entry_queue const & ) = delete;
      redo_log_rec_entry_queue &operator= ( redo_log_rec_entry_queue && ) = delete;

      void locked_push (ux_redo_lsa_log_entry &&entry);

      /* to be called after all known entries have been added
       * part of a mechanism to signal to the consumers, together with
       * whether there is still data to process, that they can bail out
       */
      void set_adding_finished();

      /* the combination of a null return value with a finished
       * flag set to true signals to the callers that no more data is expected
       * and, therefore, they can also terminate
       */
      ux_redo_lsa_log_entry locked_pop (bool &adding_finished);

      void notify_to_be_waited_for_op_finished();
      void notify_in_progress_vpid_finished (VPID _vpid);

      size_t get_dbg_stats_cons_queue_skip_count() const
      {
        return dbg_stats_cons_queue_skip_count;
      }

      size_t get_stats_spin_wait_count() const
      {
        return sbg_stats_spin_wait_count;
      }

    private:
      /* two queues are internally managed
       */
      ux_entry_deque *produce_queue;
      std::mutex produce_queue_mutex;
      ux_entry_deque *consume_queue;
      std::mutex consume_queue_mutex;

      std::atomic_bool adding_finished;

      /* barrier mechanism for log entries which are to be executed before all
       * log entries that come after them in the log
       *
       * TODO: given that such 'to_be_waited_for' log entries come in a certain order which
       * results in ever increasing VPID's, a better mechanism can be put in place where instead of
       * a single barrier for all VPID's, one barrier for each combinations of max(VOLID), max(PAGEID)
       * on a per volume basis can be used
       */
      bool to_be_waited_for_op_in_progress;
      std::condition_variable to_be_waited_for_op_in_progress_cv;

      /* bookkeeping for log entries currently in process of being applied, this
       * mechanism guarantees ordering among entries with the same VPID;
       */
      vpid_set in_progress_vpids;
      std::mutex in_progress_vpids_mutex;

      size_t dbg_stats_cons_queue_skip_count;
      size_t sbg_stats_spin_wait_count;
  };

}

#endif // LOG_RECOVERY_REDO_HPP
