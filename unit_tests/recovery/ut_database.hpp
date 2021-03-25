#ifndef MAIN_HPP
#define MAIN_HPP

//#include "log_recovery_redo.hpp"
#define LOG_RECOVERY_REDO_PARALLEL_UNIT_TEST 1
#include "log_recovery_redo_parallel.hpp"
#include "storage_common.h"
#include "vpid.hpp"

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
    redo_job_unit_test_impl (INT64 a_id, VPID a_vpid, int a_millis)
      : cublog::redo_parallel::redo_job_base(a_vpid), m_id(a_id), m_millis(a_millis)
    // VPID a_vpid, const log_lsa &a_rcv_lsa, const LOG_LSA *a_end_redo_lsa, LOG_RECTYPE a_log_rtype);
    {

    }

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
      return m_id == that.m_id
          && get_vpid () == that.get_vpid ()
          //&& m_vpid == that.m_vpid
          && m_millis == that.m_millis;
    }

  private:
    const INT64 m_id;
    //const VPID m_vpid;
    const int m_millis;
//    const log_lsa m_rcv_lsa;
//    const LOG_LSA *m_end_redo_lsa;  // by design pointer is guaranteed to outlive this instance
//    const LOG_RECTYPE m_log_rtype;
};
using ux_redo_job_unit_test_impl = std::unique_ptr<redo_job_unit_test_impl>;

/* starting point for database config
 */
class db_database_config
{
  public:
    const size_t max_volume_count_per_database;
    const size_t max_page_count_per_volume;
    const size_t max_duration_in_millis;
};


/* takes database config and generates different values
 */
class db_global_values // TODO: db_database_values_generator
{
  public:
    db_global_values (const db_database_config &a_database_config)
      : lsa_log_entry_id (10100) // just starts from an arbitrary value
      , gen (rd ())
      , duration_in_millis_dist (1, a_database_config.max_duration_in_millis)
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
// TODO: class
struct db_page
{
    db_page () = default;
    db_page (const db_page & ) = delete;
    db_page (      db_page && ) = delete;
    db_page &operator= (const db_page &) = delete;
    db_page &operator= (      db_page && ) = delete;

    void initialize (short a_volid, int32_t a_pageid)
    {
      assert (a_volid != NULL_VOLID);
      assert (a_pageid != NULL_PAGEID);
      vpid.volid = a_volid;
      vpid.pageid = a_pageid;
    }

    ux_redo_job_unit_test_impl generate_changes (const db_global_values &a_db_global_values)
    {
      const INT64 lsa_log_entry_id = a_db_global_values.increment_lsa_log_entry_id();
      const int millis = a_db_global_values.duration_in_millis();

      ux_redo_job_unit_test_impl job_to_append
      {
        new redo_job_unit_test_impl (lsa_log_entry_id, vpid, millis)
      };
      entries.push_front (std::move (job_to_append));

      ux_redo_job_unit_test_impl job_to_return
      {
        new redo_job_unit_test_impl (lsa_log_entry_id, vpid, millis)
      };
      return job_to_return;
    }

//    void apply_changes (log_recovery::ux_redo_lsa_log_entry &&_lsa_log_entry)
//    {
//      // NOTE: although function is to be called from different threads, the function
//      // is intentionally not locked as the intention is to check proper synchronization in
//      // the redo log apply algorithm
//      std::lock_guard<std::mutex> lock (apply_changes_mutex);
//      entries.push_front (std::move (_lsa_log_entry));
//    }

    bool operator== (const db_page &that) const
    {
      auto this_entries_it = entries.cbegin ();
      auto that_entries_it = that.entries.cbegin ();
      while (this_entries_it != entries.cend () && that_entries_it != that.entries.cend ())
	{
	  if (! (**this_entries_it == **that_entries_it))
	    {
	      return false;
	    }
	  ++this_entries_it;
	  ++that_entries_it;
	}
      if (this_entries_it != entries.cend () || that_entries_it != that.entries.cend ())
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
    db_volume (const db_database_config &a_database_config)
      : m_database_config(a_database_config)
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

    ux_redo_job_unit_test_impl generate_changes (const db_global_values &_db_global_values)
    {
      const int update_or_add_page = rand_update_or_add_page();
      if (update_or_add_page == 0 && m_pages.size() > 0)
        {
          // invoke existing pages to generate changes
          const int page_index = rand_update_page_index();
          const auto &page = m_pages.at (page_index);
          return page->generate_changes (_db_global_values);
        }
      else
        {
          if (update_or_add_page == 1 || m_pages.size() == 0)
            {
              // add new page and generate log entry
              // akin to 'extend volume' operation
              const auto lsa_log_entry_id = _db_global_values.increment_lsa_log_entry_id();
              const int millis = _db_global_values.duration_in_millis();
              ux_redo_job_unit_test_impl job
              {
                new redo_job_unit_test_impl (lsa_log_entry_id, { /*static_cast<short> (pages.size())*/ NULL_PAGEID, volid }, millis)
              };
              add_new_page (m_pages);
              return job;
            }
          else
            {
              assert (false);
            }
        }
      return nullptr;
    }

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
      if (volid != _that.volid || m_pages.size () != _that.m_pages.size ())
	{
	  return false;
	}
      auto this_pages_it = m_pages.cbegin ();
      auto that_pages_it = _that.m_pages.cbegin ();
      while (this_pages_it != m_pages.cend () && that_pages_it != _that.m_pages.cend ())
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
     *
     * // TODO: add delete page
     */
    int rand_update_or_add_page ()
    {
      if (m_pages.size ()< m_database_config.max_page_count_per_volume)
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
      int range = m_pages.size ();
      int res = rand () % range;
      return res;
    }

  private:
    const db_database_config &m_database_config;

    short volid = NULL_VOLID;

    // page index in vector == page id
    std::vector<ux_db_page> m_pages;

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
    db_database (const db_database_config &a_database_config)
      : m_database_config(a_database_config)
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

    ux_redo_job_unit_test_impl generate_changes (const db_global_values &_db_global_values)
    {
      const int update_or_add_volume = rand_update_or_add_volume();
      if (update_or_add_volume == 0 && m_volumes.size() > 0)
        {
          // invoke existing volume to generate changes
          const int vol_index = rand_update_volume_index();
          const auto &vol = m_volumes.at (vol_index);
          return vol->generate_changes (_db_global_values);
        }
      else
        {
          if (update_or_add_volume == 1 || m_volumes.size() == 0)
            {
              // add new volume and generate log entry
              const auto lsa_log_entry_id = _db_global_values.increment_lsa_log_entry_id();
              const int millis = _db_global_values.duration_in_millis();
              ux_redo_job_unit_test_impl job
              {
                new redo_job_unit_test_impl (lsa_log_entry_id, { NULL_PAGEID, /*static_cast<short> (volumes.size())*/ NULL_VOLID }, millis)
              };
              add_new_volume (m_volumes);
              return job;
            }
          else
            {
              assert (false);
            }
        }
      return nullptr;
    }

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
      if (m_volumes.size () != _that.m_volumes.size ())
	{
	  return false;
	}
      auto this_volumes_it = m_volumes.cbegin ();
      auto that_volumes_it = _that.m_volumes.cbegin ();
      while (this_volumes_it != m_volumes.cend () && that_volumes_it != _that.m_volumes.cend ())
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
      ux_db_volume vol { new db_volume (m_database_config) };
      vol->initialize (_volumes.size ());
      _volumes.push_back (std::move (vol));
      return *_volumes.rbegin ();
    }

    /* 0 - update
     * 1 - new volume
     */
    int rand_update_or_add_volume ()
    {
      if (m_volumes.size ()< m_database_config.max_volume_count_per_database)
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
      size_t range = m_volumes.size ();
      size_t res = rand () % range;
      return res;
    }

  private:
    const db_database_config &m_database_config;

    // volume index in vector == volume id
    std::vector<ux_db_volume> m_volumes;

    // discrete distribution for adding new volumes or updating existing volumes
    std::random_device rd;
    mutable std::mt19937 gen;
    mutable std::discrete_distribution<int8_t> update_or_add_volume_dist;

    std::mutex m_apply_changes_mutex;
};
using ux_db_database = std::unique_ptr<db_database>;

/*
 */
//struct consumption_accumulator
//{
//    consumption_accumulator ()
//    {
//    }

//    void accumulate (std::string &&_value)
//    {
//      std::lock_guard<std::mutex> lck (mtx);
//      data.push_back (std::move (_value));
//    }

//    const std::vector<std::string> &get_data () const
//    {
//      return data;
//    }

//  private:
//    std::mutex mtx;
//    std::vector<std::string> data;
//};

#endif // MAIN_HPP
