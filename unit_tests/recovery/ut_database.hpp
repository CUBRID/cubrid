#ifndef MAIN_HPP
#define MAIN_HPP

// deactivate certain asserts in the implementation specifically for unit tests
#define LOG_RECOVERY_REDO_PARALLEL_UNIT_TEST
#include "log_recovery_redo_parallel.hpp"
#undef LOG_RECOVERY_REDO_PARALLEL_UNIT_TEST

#include "storage_common.h"
#include "vpid.hpp"

#include <forward_list>
#include <mutex>
#include <random>
#include <string>
#include <vector>

static constexpr short ADD_VOLUME_DISCRETE_RATIO = 1;
static constexpr short UPDATE_VOLUME_DISCRETE_RATIO = 39;

static constexpr short ADD_PAGE_DISCRETE_RATIO = 1;
static constexpr short UPDATE_PAGE_DISCRETE_RATIO = 19;

// forward declarations
class ut_database;

class ut_redo_job_impl;
using ux_ut_redo_job_impl = std::unique_ptr<ut_redo_job_impl>;

/*
 */
class ut_redo_job_impl final : public cublog::redo_parallel::redo_job_base
{
  public:
    ut_redo_job_impl (ut_database &a_database_recovery, INT64 a_id, VPID a_vpid, int a_millis)
      : cublog::redo_parallel::redo_job_base(a_vpid)
      , m_database_recovery(a_database_recovery), m_id(a_id), m_millis(a_millis)
    {
    }

    ut_redo_job_impl (ut_redo_job_impl const &) = delete;
    ut_redo_job_impl (ut_redo_job_impl &&) = delete;

    ~ut_redo_job_impl () override = default;

    ut_redo_job_impl &operator = (ut_redo_job_impl const &) = delete;
    ut_redo_job_impl &operator = (ut_redo_job_impl &&) = delete;

    int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
                 LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) override;

    bool operator == (const ut_redo_job_impl &that) const
    {
      return m_id == that.m_id
          && get_vpid () == that.get_vpid ()
          //&& m_vpid == that.m_vpid
          && m_millis == that.m_millis;
    }

    // TOTO: extra enum discriminant iso functions below

    bool is_volume_creation() const
    {
      const auto &vpid = get_vpid ();
      return vpid.volid == NULL_VOLID && vpid.pageid == NULL_PAGEID;
    }

    bool is_page_creation() const
    {
      const auto &vpid = get_vpid ();
      return vpid.volid != NULL_VOLID && vpid.pageid == NULL_PAGEID;
    }

    bool is_page_modification() const
    {
      const auto &vpid = get_vpid ();
      return vpid.volid != NULL_VOLID && vpid.pageid != NULL_PAGEID;
    }

  private:
    ux_ut_redo_job_impl clone()
    {
      const auto &vpid = get_vpid ();
      ux_ut_redo_job_impl res { new ut_redo_job_impl (m_database_recovery, m_id, vpid, m_millis) };
      return res;
    }

  private:
    ut_database &m_database_recovery;

    const INT64 m_id;
    const int m_millis;
};

/* database config values
 */
struct ut_database_config
{
  const size_t max_volume_count_per_database;
  const size_t max_page_count_per_volume;

  const size_t max_duration_in_millis;
};

/* takes database config and generates different values
 */
class ut_database_values_generator
{
  public:
    /* either add or update a certain entity
     */
    enum class add_or_update
    {
      ADD,
      UPDATE
    };

  public:
    ut_database_values_generator (const ut_database_config &a_database_config)
      : m_database_config(a_database_config)
      , m_lsa_log_id (10100) // just starts from an arbitrary value
      , gen (rd ())
      , duration_in_millis_dist (1, a_database_config.max_duration_in_millis)
      , add_or_update_volume_dist ({ADD_VOLUME_DISCRETE_RATIO, UPDATE_VOLUME_DISCRETE_RATIO})
      , add_or_update_page_dist ({ADD_PAGE_DISCRETE_RATIO, UPDATE_PAGE_DISCRETE_RATIO})
    {
    }

    INT64 increment_and_get_lsa_log_id ()
    {
      return ++m_lsa_log_id;
    }

    int duration_in_millis ()
    {
      return duration_in_millis_dist (gen);
    }

    add_or_update rand_add_or_update_volume (const size_t a_current_volume_count)
    {
      if (a_current_volume_count < m_database_config.max_volume_count_per_database)
        {
          // while there is still space to add volumes, choose randomly
          const int8_t random_value = add_or_update_volume_dist (gen);
          return (random_value == 0)
              ? add_or_update::ADD
              : add_or_update::UPDATE;
        }
      else
        {
          // once the maximum number of volumes has been reached, just update
          return add_or_update::UPDATE;
        }
    }

    /* uniform distribution for updating entities
     */
    size_t rand_index_of_entity_to_update (const size_t a_current_entity_count)
    {
      const size_t res = (rand () * rand ()) % a_current_entity_count;
      return res;
    }

    add_or_update rand_add_or_update_page (const size_t a_current_page_count)
    {
      if (a_current_page_count < m_database_config.max_page_count_per_volume)
        {
          // while there is still space to add pages, choose randomly
          const int8_t random_value = add_or_update_page_dist (gen);
          return (random_value == 0)
              ? add_or_update::ADD
              : add_or_update::UPDATE;
        }
      else
        {
          // once the maximum number of pages per volume has been reached, just update
          return add_or_update::UPDATE;
        }
    }

  private:
    const ut_database_config &m_database_config;

    // just a global ever increasing id
    mutable INT64 m_lsa_log_id;

    std::random_device rd;
    mutable std::mt19937 gen;

    // number of millis for a task to busy wait
    mutable std::uniform_int_distribution<int> duration_in_millis_dist;

    // discrete distribution for adding new volumes or updating existing volumes
    mutable std::discrete_distribution<int8_t> add_or_update_volume_dist;

    // discrete distribution for adding new pages or updating existing pages
    mutable std::discrete_distribution<int8_t> add_or_update_page_dist;
};

/*
 */
class ut_page
{
  public:
    ut_page () = default;
    ut_page (const ut_page & ) = delete;
    ut_page (      ut_page && ) = delete;
    ut_page &operator= (const ut_page &) = delete;
    ut_page &operator= (      ut_page && ) = delete;

    void initialize (short a_volid, int32_t a_pageid)
    {
      assert (a_volid != NULL_VOLID);
      assert (a_pageid != NULL_PAGEID);
      m_vpid.volid = a_volid;
      m_vpid.pageid = a_pageid;
    }

    const VPID &get_vpid() const
    {
      return m_vpid;
    }

    ux_ut_redo_job_impl generate_changes (ut_database &a_database_recovery, ut_database_values_generator &a_db_global_values)
    {
      const INT64 lsa_log_id = a_db_global_values.increment_and_get_lsa_log_id();
      const int millis = a_db_global_values.duration_in_millis();

      ux_ut_redo_job_impl job_to_append
      {
        new ut_redo_job_impl (a_database_recovery, lsa_log_id, m_vpid, millis)
      };
      m_entries.push_front (std::move (job_to_append));

      ux_ut_redo_job_impl job_to_return
      {
        new ut_redo_job_impl (a_database_recovery, lsa_log_id, m_vpid, millis)
      };
      return job_to_return;
    }

    void apply_changes (ux_ut_redo_job_impl &&a_job)
    {
      // NOTE: although function is to be called from different threads, the function
      // is intentionally not locked as the intention is to check proper synchronization in
      // the redo log apply algorithm
      std::lock_guard<std::mutex> lock (m_apply_changes_mtx);

      m_entries.push_front (std::move (a_job));
    }

    bool operator== (const ut_page &that) const
    {
      auto this_entries_it = m_entries.cbegin ();
      auto that_entries_it = that.m_entries.cbegin ();
      while (this_entries_it != m_entries.cend () && that_entries_it != that.m_entries.cend ())
	{
	  if (! (**this_entries_it == **that_entries_it))
	    {
	      return false;
	    }
	  ++this_entries_it;
	  ++that_entries_it;
	}
      if (this_entries_it != m_entries.cend () || that_entries_it != that.m_entries.cend ())
	{
	  return false;
	}
      return true;
    }

  private:
    VPID m_vpid = VPID_INITIALIZER;

  private:
    std::forward_list<ux_ut_redo_job_impl> m_entries;

    std::mutex m_apply_changes_mtx;
};
using ux_ut_page = std::unique_ptr<ut_page>;

/*
 */
class ut_volume
{
  public:
    ut_volume (const ut_database_config &a_database_config)
      : m_database_config(a_database_config)
    {
    }

    ut_volume (const ut_volume & ) = delete;
    ut_volume (      ut_volume && ) = delete;

    ut_volume &operator= (const ut_volume &) = delete;
    ut_volume &operator= (      ut_volume && ) = delete;

    void initialize (short a_volid)
    {
      assert (a_volid != NULL_VOLID);
      volid = a_volid;
      // start without any pages
    }

    short get_volid () const
    {
      return volid;
    }

    ux_ut_redo_job_impl generate_changes (ut_database &a_database_recovery, ut_database_values_generator &a_db_global_values)
    {
      const ut_database_values_generator::add_or_update add_or_update_page
          = a_db_global_values.rand_add_or_update_page (m_pages.size ());
      if (add_or_update_page == ut_database_values_generator::add_or_update::UPDATE
          && m_pages.size() > 0)
        {
          // invoke existing pages to generate changes
          const int page_index = rand_update_page_index();
          const auto &page = m_pages.at (page_index);
          return page->generate_changes (a_database_recovery, a_db_global_values);
        }
      else
        {
          if (add_or_update_page == ut_database_values_generator::add_or_update::ADD
              || m_pages.size() == 0)
            {
              // add new page and generate log entry
              // akin to 'extend volume' operation
              const auto lsa_log_id = a_db_global_values.increment_and_get_lsa_log_id();
              const int millis = a_db_global_values.duration_in_millis();
              ux_ut_redo_job_impl job
              {
                new ut_redo_job_impl (a_database_recovery, lsa_log_id, { /*static_cast<short> (pages.size())*/ NULL_PAGEID, volid }, millis)
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

    void apply_changes (ux_ut_redo_job_impl &&a_job)
    {
      // NOTE: although function is to be called from different threads, the function
      // is intentionally not locked as the intention is to check proper synchronization in
      // the redo log apply algorithm
      if (a_job->is_volume_creation ())
        {
          assert (false);
        }
      else
        {
          if (a_job->is_page_creation ())
            {
              std::lock_guard<std::mutex> lock (m_apply_changes_mtx);
              const ux_ut_page &new_page = add_new_page (m_pages);
              assert (new_page->get_vpid ().volid == a_job->get_vpid().volid);
            }
          else
            {
              std::lock_guard<std::mutex> lock (m_apply_changes_mtx);
              ux_ut_page &page = m_pages.at (a_job->get_vpid().pageid);
              page->apply_changes (std::move (a_job));
            }
        }
    }

    bool operator== (const ut_volume &that) const
    {
      if (volid != that.volid || m_pages.size () != that.m_pages.size ())
	{
	  return false;
	}
      auto this_pages_it = m_pages.cbegin ();
      auto that_pages_it = that.m_pages.cbegin ();
      while (this_pages_it != m_pages.cend () && that_pages_it != that.m_pages.cend ())
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
    const ux_ut_page &add_new_page (std::vector<ux_ut_page> &a_pages)
    {
      ux_ut_page page { new ut_page () };
      page->initialize (volid, a_pages.size ());
      a_pages.push_back (std::move (page));
      return *a_pages.rbegin ();
    }

    // uniform distribution for updating pages
    int rand_update_page_index ()
    {
      int range = m_pages.size ();
      int res = rand () % range;
      return res;
    }

  private:
    const ut_database_config &m_database_config;

    short volid = NULL_VOLID;

    // page index in vector == page id
    std::vector<ux_ut_page> m_pages;

    std::mutex m_apply_changes_mtx;
};
using ux_ut_volume = std::unique_ptr<ut_volume>;

/*
 */
class ut_database
{
  public:
    ut_database (const ut_database_config &a_database_config)
      : m_database_config(a_database_config)
    {
    }

    ut_database (const ut_database & ) = delete;
    ut_database (      ut_database && ) = delete;

    ut_database &operator= (const ut_database &) = delete;
    ut_database &operator= (      ut_database && ) = delete;

    void initialize ()
    {
      // start without any volumes
    }

    ux_ut_redo_job_impl generate_changes (ut_database &a_database_recovery, ut_database_values_generator &a_db_global_values)
    {
      //const int update_or_add_volume = rand_update_or_add_volume();
      const auto add_or_update_volume = a_db_global_values.rand_add_or_update_volume (m_volumes.size ());
      if (add_or_update_volume == ut_database_values_generator::add_or_update::UPDATE
          && m_volumes.size() > 0)
        {
          // invoke existing volume to generate changes
          const int vol_index = a_db_global_values.rand_index_of_entity_to_update (m_volumes.size());
          const auto &vol = m_volumes.at (vol_index);
          return vol->generate_changes (a_database_recovery, a_db_global_values);
        }
      else
        {
          if (add_or_update_volume == ut_database_values_generator::add_or_update::ADD
              || m_volumes.size() == 0)
            {
              // add new volume and generate log entry
              const auto lsa_log_id = a_db_global_values.increment_and_get_lsa_log_id();
              const int millis = a_db_global_values.duration_in_millis();
              ux_ut_redo_job_impl job
              {
                new ut_redo_job_impl (a_database_recovery, lsa_log_id, { NULL_PAGEID, /*static_cast<short> (volumes.size())*/ NULL_VOLID }, millis)
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

    void apply_changes (ux_ut_redo_job_impl &&a_job)
    {
      // NOTE: although function is to be called from different threads, the function
      // is intentionally not locked as the intention is to check proper synchronization in
      // the redo log apply algorithm
      if (a_job->is_volume_creation())
        {
          std::lock_guard<std::mutex> lock (m_apply_changes_mtx);
          const auto &new_volume = add_new_volume (m_volumes);
          // TODO: for this to work, a new enum (new_volume, new_page, alter_page) must be added and used
          // as discriminant instead of the vpid and the vpid to be only used for validation
          //assert (new_volume->get_volid () == a_job->get_vpid ().volid);
        }
      else
        {
          std::lock_guard<std::mutex> lock (m_apply_changes_mtx);
          assert (a_job->is_page_creation () || a_job->is_page_modification ());
          auto &volume = m_volumes.at (a_job->get_vpid().volid);
          volume->apply_changes (std::move (a_job));
        }
    }

    bool operator== (const ut_database &that) const
    {
      if (m_volumes.size () != that.m_volumes.size ())
	{
	  return false;
	}
      auto this_volumes_it = m_volumes.cbegin ();
      auto that_volumes_it = that.m_volumes.cbegin ();
      while (this_volumes_it != m_volumes.cend () && that_volumes_it != that.m_volumes.cend ())
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
    const ux_ut_volume &add_new_volume (std::vector<ux_ut_volume> &a_volumes)
    {
      ux_ut_volume vol { new ut_volume (m_database_config) };
      vol->initialize (a_volumes.size ());
      a_volumes.push_back (std::move (vol));
      return *a_volumes.rbegin ();
    }

  private:
    const ut_database_config &m_database_config;

    // volume index in vector == volume id
    std::vector<ux_ut_volume> m_volumes;

    std::mutex m_apply_changes_mtx;
};
using ux_ut_database = std::unique_ptr<ut_database>;

/*
 */
//struct consumption_accumulator
//{
//    consumption_accumulator ()
//    {
//    }

//    void accumulate (std::string &&a_value)
//    {
//      std::lock_guard<std::mutex> lck (mtx);
//      data.push_back (std::move (a_value));
//    }

//    const std::vector<std::string> &get_data () const
//    {
//      return data;
//    }

//  private:
//    std::mutex mtx;
//    std::vector<std::string> data;
//};


/*
 * implementations
 */

int ut_redo_job_impl::execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
                               LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support)
{
  auto my_clone = clone();
  m_database_recovery.apply_changes (std::move (my_clone));
  return 0;
}

#endif // MAIN_HPP
