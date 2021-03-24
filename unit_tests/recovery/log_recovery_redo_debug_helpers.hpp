#ifndef MAIN_HPP
#define MAIN_HPP

//#include "log_recovery_redo.hpp"
#include "log_recovery_redo_parallel.hpp"
#include "storage_common.h"

#include <forward_list>
#include <mutex>
#include <random>
#include <string>
#include <vector>

static constexpr short UPDATE_VOLUME_DISCRETE_RATIO = 39;
static constexpr short ADD_VOLUME_DISCRETE_RATIO = 1;

static constexpr short UPDATE_PAGE_DISCRETE_RATIO = 19;
static constexpr short ADD_PAGE_DISCRETE_RATIO = 1;

/*
 */
class redo_job_unit_test_impl final : public cublog::redo_parallel::redo_job_base
{
  public:
    redo_job_unit_test_impl (); // VPID a_vpid, const log_lsa &a_rcv_lsa, const LOG_LSA *a_end_redo_lsa, LOG_RECTYPE a_log_rtype);

    redo_job_unit_test_impl (redo_job_unit_test_impl const &) = delete;
    redo_job_unit_test_impl (redo_job_unit_test_impl &&) = delete;

    ~redo_job_unit_test_impl () override = default;

    redo_job_unit_test_impl &operator = (redo_job_unit_test_impl const &) = delete;
    redo_job_unit_test_impl &operator = (redo_job_unit_test_impl &&) = delete;

    int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
		 LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) override
    {
      assert (false);
      return -1;
    }

    bool operator == (const redo_job_unit_test_impl &that) const
    {
      assert (false);
      return false;
    }

  private:
//    const log_lsa m_rcv_lsa;
//    const LOG_LSA *m_end_redo_lsa;  // by design pointer is guaranteed to outlive this instance
//    const LOG_RECTYPE m_log_rtype;
};
using ux_redo_job_unit_test_impl = std::unique_ptr<redo_job_unit_test_impl>;

/*
 */
class db_global_values
{
  public:
    db_global_values ()
      : lsa_log_entry_id (10100) // just starts from an arbitrary value
      , gen (rd ())
      , duration_in_millis_dist (1, 3)
    {}

    INT64 increment_lsa_log_entry_id () const
    {
      return ++lsa_log_entry_id;
    }

    int duration_in_millis () const
    {
      return duration_in_millis_dist (gen);
    }

  private:

    mutable INT64 lsa_log_entry_id;

    std::random_device rd;
    mutable std::mt19937 gen;
    mutable std::uniform_int_distribution<int> duration_in_millis_dist;
};

/*
 */
struct db_page
{
    db_page () = default;

    db_page (const db_page & ) = delete;
    db_page (      db_page && ) = delete;
    db_page &operator= (const db_page &) = delete;
    db_page &operator= (      db_page && ) = delete;

    void initialize (short _volid, int32_t _pageid)
    {
      assert (_volid != NULL_VOLID);
      assert (_pageid != NULL_PAGEID);
      vpid.volid = _volid;
      vpid.pageid = _pageid;
    }

//    log_recovery_ns::ux_redo_lsa_log_entry generate_changes (const db_global_values &_db_global_values)
//    {
//      const auto lsa_log_entry_id = _db_global_values.increment_lsa_log_entry_id();
//      const int millis = _db_global_values.duration_in_millis();

//      log_recovery::ux_redo_lsa_log_entry entry_to_append
//      {
//        new log_recovery::redo_log_rec_entry (lsa_log_entry_id, vpid, millis)
//      };
//      entries.push_front (std::move (entry_to_append));

//      log_recovery::ux_redo_lsa_log_entry entry_to_return
//      {
//        new log_recovery::redo_log_rec_entry (lsa_log_entry_id, vpid, millis)
//      };
//      return entry_to_return;
//    }

//    void apply_changes (log_recovery::ux_redo_lsa_log_entry &&_lsa_log_entry)
//    {
//      // NOTE: although function is to be called from different threads, the function
//      // is intentionally not locked as the intention is to check proper synchronization in
//      // the redo log apply algorithm
//      std::lock_guard<std::mutex> lock (apply_changes_mutex);
//      entries.push_front (std::move (_lsa_log_entry));
//    }

    bool operator== (const db_page &_that) const
    {
      auto this_entries_it = entries.cbegin ();
      auto that_entries_it = _that.entries.cbegin ();
      while (this_entries_it != entries.cend () && that_entries_it != _that.entries.cend ())
	{
	  if (! (**this_entries_it == **that_entries_it))
	    {
	      return false;
	    }
	  ++this_entries_it;
	  ++that_entries_it;
	}
      if (this_entries_it != entries.cend () || that_entries_it != _that.entries.cend ())
	{
	  return false;
	}
      return true;
    }

  public:
    VPID vpid = VPID_INITIALIZER;

  private:
    std::forward_list<ux_redo_job_unit_test_impl> entries;

    std::mutex apply_changes_mutex;
};
using ux_db_page = std::unique_ptr<db_page>;

/*
 */
class db_volume
{
  public:
    db_volume (size_t _max_page_count_per_volume)
      : max_page_count_per_volume (_max_page_count_per_volume)
      , gen (rd ()), update_or_add_page_dist ({UPDATE_PAGE_DISCRETE_RATIO, ADD_PAGE_DISCRETE_RATIO})
    { }
    db_volume (const db_volume & ) = delete;
    db_volume (      db_volume && ) = delete;
    db_volume &operator= (const db_volume &) = delete;
    db_volume &operator= (      db_volume && ) = delete;

    void initialize (short _volid)
    {
      assert (_volid != NULL_VOLID);
      volid = _volid;
      // start without any pages
    }

    short get_volid () const
    {
      return volid;
    }

//    log_recovery::ux_redo_lsa_log_entry generate_changes (const db_global_values &_db_global_values)
//    {
//      const int update_or_add_page = rand_update_or_add_page();
//      if (update_or_add_page == 0 && pages.size() > 0)
//        {
//          // invoke existing pages to generate changes
//          const int page_index = rand_update_page_index();
//          const auto &page = pages.at (page_index);
//          return page->generate_changes (_db_global_values);
//        }
//      else
//        {
//          if (update_or_add_page == 1 || pages.size() == 0)
//            {
//              // add new page and generate log entry
//              // akin to 'extend volume' operation
//              const auto lsa_log_entry_id = _db_global_values.increment_lsa_log_entry_id();
//              const int millis = _db_global_values.duration_in_millis();
//              log_recovery::ux_redo_lsa_log_entry entry
//              {
//                new log_recovery::redo_log_rec_entry (lsa_log_entry_id, { /*static_cast<short> (pages.size())*/ NULL_PAGEID, volid }, millis)
//              };
//              add_new_page (pages);
//              return entry;
//            }
//          else
//            {
//              assert (false);
//            }
//        }
//      return nullptr;
//    }

//    void apply_changes (log_recovery::ux_redo_lsa_log_entry &&_lsa_log_entry)
//    {
//      // NOTE: although function is to be called from different threads, the function
//      // is intentionally not locked as the intention is to check proper synchronization in
//      // the redo log apply algorithm
//      if (_lsa_log_entry->is_volume_creation())
//        {
//          assert (false);
//        }
//      else
//        {
//          if (_lsa_log_entry->is_volume_extension())
//            {
//              std::lock_guard<std::mutex> lock (apply_changes_mutex);
//              const ux_db_page &new_page = add_new_page (pages);
//              assert (new_page->vpid.volid == _lsa_log_entry->get_vpid().volid);
//            }
//          else
//            {
//              std::lock_guard<std::mutex> lock (apply_changes_mutex);
//              ux_db_page &page = pages.at (_lsa_log_entry->get_vpid().pageid);
//              page->apply_changes (std::move (_lsa_log_entry));
//            }
//        }
//    }

    bool operator== (const db_volume &_that) const
    {
      if (volid != _that.volid || pages.size () != _that.pages.size ())
	{
	  return false;
	}
      auto this_pages_it = pages.cbegin ();
      auto that_pages_it = _that.pages.cbegin ();
      while (this_pages_it != pages.cend () && that_pages_it != _that.pages.cend ())
	{
	  if (! (**this_pages_it == **that_pages_it))
	    {
	      return false;
	    }
	  ++this_pages_it;
	  ++that_pages_it;
	}
      return true;
    }

  private:
    const ux_db_page &add_new_page (std::vector<ux_db_page> &_pages)
    {
      ux_db_page page { new db_page () };
      page->initialize (volid, _pages.size ());
      _pages.push_back (std::move (page));
      return *_pages.rbegin ();
    }

    /* 0 - update
     * 1 - new page
     */
    int rand_update_or_add_page ()
    {
      if (pages.size ()< max_page_count_per_volume)
	{
	  return update_or_add_page_dist (gen);
	}
      else
	{
	  return 0; // just update once the maximum number of pages per volume has been reached
	}
    }

    // uniform distribution for updating pages
    int rand_update_page_index ()
    {
      int range = pages.size ();
      int res = rand () % range;
      return res;
    }

  private:
    size_t max_page_count_per_volume;

    short volid = NULL_VOLID;

    // page index in vector == page id
    std::vector<ux_db_page> pages;

    // discrete distribution for adding new pages or updating existing pages
    std::random_device rd;
    mutable std::mt19937 gen;
    mutable std::discrete_distribution<int8_t> update_or_add_page_dist;

    std::mutex apply_changes_mutex;
};
using ux_db_volume = std::unique_ptr<db_volume>;

/*
 */
class db_database
{
  public:
    db_database (size_t _max_volume_count_per_database, size_t _max_page_count_per_volume)
      : max_volume_count_per_database (_max_volume_count_per_database), max_page_count_per_volume (_max_page_count_per_volume)
      , gen (rd ()), update_or_add_volume_dist ({UPDATE_VOLUME_DISCRETE_RATIO, ADD_VOLUME_DISCRETE_RATIO})
    {}
    db_database (const db_database & ) = delete;
    db_database (      db_database && ) = delete;
    db_database &operator= (const db_database &) = delete;
    db_database &operator= (      db_database && ) = delete;

    void initialize ()
    {
      // start without any volumes
    }

//    ux_redo_job_unit_test_impl generate_changes (const db_global_values &_db_global_values)
//    {
//      const int update_or_add_volume = rand_update_or_add_volume();
//      if (update_or_add_volume == 0 && volumes.size() > 0)
//        {
//          // invoke existing volume to generate changes
//          const int vol_index = rand_update_volume_index();
//          const auto &vol = volumes.at (vol_index);
//          return vol->generate_changes (_db_global_values);
//        }
//      else
//        {
//          if (update_or_add_volume == 1 || volumes.size() == 0)
//            {
//              // add new volume and generate log entry
//              const auto lsa_log_entry_id = _db_global_values.increment_lsa_log_entry_id();
//              const int millis = _db_global_values.duration_in_millis();
//              log_recovery::ux_redo_lsa_log_entry entry
//              {
//                new log_recovery::redo_log_rec_entry (lsa_log_entry_id, { NULL_PAGEID, /*static_cast<short> (volumes.size())*/ NULL_VOLID }, millis)
//              };
//              add_new_volume (volumes);
//              return entry;
//            }
//          else
//            {
//              assert (false);
//            }
//        }
//      return nullptr;
//    }

//    void apply_changes (log_recovery::ux_redo_lsa_log_entry &&_lsa_log_entry)
//    {
//      // NOTE: although function is to be called from different threads, the function
//      // is intentionally not locked as the intention is to check proper synchronization in
//      // the redo log apply algorithm
//      if (_lsa_log_entry->is_volume_creation())
//        {
//          std::lock_guard<std::mutex> lock (apply_changes_mutex);
//          const auto &new_volume = add_new_volume (volumes);
//          //assert (new_volume->get_volid() == _lsa_log_entry->get_vpid().volid);
//        }
//      else
//        {
//          std::lock_guard<std::mutex> lock (apply_changes_mutex);
//          assert (_lsa_log_entry->is_volume_extension() || _lsa_log_entry->is_page_modification());
//          auto &volume = volumes.at (_lsa_log_entry->get_vpid().volid);
//          volume->apply_changes (std::move (_lsa_log_entry));
//        }
//    }

    bool operator== (const db_database &_that) const
    {
      if (volumes.size () != _that.volumes.size ())
	{
	  return false;
	}
      auto this_volumes_it = volumes.cbegin ();
      auto that_volumes_it = _that.volumes.cbegin ();
      while (this_volumes_it != volumes.cend () && that_volumes_it != _that.volumes.cend ())
	{
	  if (! (**this_volumes_it == **that_volumes_it))
	    {
	      return false;
	    }
	  ++this_volumes_it;
	  ++that_volumes_it;
	}
      return true;
    }


  private:
    const ux_db_volume &add_new_volume (std::vector<ux_db_volume> &_volumes)
    {
      ux_db_volume vol { new db_volume (max_page_count_per_volume) };
      vol->initialize (_volumes.size ());
      _volumes.push_back (std::move (vol));
      return *_volumes.rbegin ();
    }

    /* 0 - update
     * 1 - new volume
     */
    int rand_update_or_add_volume ()
    {
      if (volumes.size ()< max_volume_count_per_database)
	{
	  return update_or_add_volume_dist (gen);
	}
      else
	{
	  return 0; // just update once the maximum number of volumes per database has been reached
	}
    }

    /* uniform distribution for updating volumes
     */
    size_t rand_update_volume_index ()
    {
      size_t range = volumes.size ();
      size_t res = rand () % range;
      return res;
    }

  private:
    size_t max_volume_count_per_database;
    size_t max_page_count_per_volume;

    // volume index in vector == volume id
    std::vector<ux_db_volume> volumes;

    // discrete distribution for adding new volumes or updating existing volumes
    std::random_device rd;
    mutable std::mt19937 gen;
    mutable std::discrete_distribution<int8_t> update_or_add_volume_dist;

    std::mutex apply_changes_mutex;
};
using ux_db_database = std::unique_ptr<db_database>;

/*
 */
struct consumption_accumulator
{
    consumption_accumulator ()
    {
    }

    void accumulate (std::string &&_value)
    {
      std::lock_guard<std::mutex> lck (mtx);
      data.push_back (std::move (_value));
    }

    const std::vector<std::string> &get_data () const
    {
      return data;
    }

  private:
    std::mutex mtx;
    std::vector<std::string> data;
};

#endif // MAIN_HPP
