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

#ifndef LOG_READER_HPP
#define LOG_READER_HPP

#include "log_storage.hpp"
#include "log_impl.h"

#include "log_lsa.hpp"

#include "thread_manager.hpp"

#include <type_traits>

inline void
LOG_READ_ALIGN (THREAD_ENTRY *thread_p, LOG_LSA *lsa, LOG_PAGE *log_pgptr,
		log_cs_access_mode cs_access_mode = LOG_CS_FORCE_USE);
inline void
LOG_READ_ADD_ALIGN (THREAD_ENTRY *thread_p, size_t add, LOG_LSA *lsa, LOG_PAGE *log_pgptr,
		    log_cs_access_mode cs_access_mode = LOG_CS_FORCE_USE);
inline void
LOG_READ_ADVANCE_WHEN_DOESNT_FIT (THREAD_ENTRY *thread_p, size_t length, LOG_LSA *lsa,
				  LOG_PAGE *log_pgptr,
				  log_cs_access_mode cs_access_mode = LOG_CS_FORCE_USE);

/* encapsulates reading of the log
 *
 * NOTE: not thread safe
 * NOTE: improvement: introduce an internal buffer to be used for copying and serving
 *    data from the log of arbitrary size; currently this is done manually outside this class
 *    using a support buffer structure of
 */
class log_reader final
{
  public:
    inline log_reader ()
    {
      m_page = reinterpret_cast<log_page *> (PTR_ALIGN (m_area_buffer, MAX_ALIGNMENT));
    }
    log_reader (log_reader const & ) = delete;
    log_reader (log_reader && ) = delete;

    log_reader &operator = (log_reader const & ) = delete;
    log_reader &operator = (log_reader && ) = delete;

    enum class fetch_mode
    {
      NORMAL,
      FORCE
    };

    inline const log_lsa &get_lsa () const
    {
      return m_lsa;
    }

    inline std::int64_t get_pageid () const
    {
      return m_lsa.pageid;
    }

    inline std::int16_t get_offset () const
    {
      return m_lsa.offset;
    }

    inline int set_lsa_and_fetch_page (const log_lsa &lsa, fetch_mode fetch_page_mode = fetch_mode::NORMAL)
    {
      const bool do_fetch_page { fetch_page_mode == fetch_mode::FORCE || m_lsa.pageid != lsa.pageid };
      m_lsa = lsa;
      if (do_fetch_page)
	{
	  THREAD_ENTRY *const thread_entry = get_thread_entry ();
	  assert (thread_entry == &cubthread::get_entry ());
	  return fetch_page (thread_entry);
	}
      return NO_ERROR;
    }
    inline const log_hdrpage &get_page_header () const
    {
      return m_page->hdr;
    }

    inline const log_page *get_page () const
    {
      return m_page;
    }

    /*
     * Note: `remove_reference` helps if function is called with a typedef
     * that is actually a reference
     * eg:
     *    ..reinterpret_cptr<DUMMY_TYPE>()
     * where `DUMMY_TYPE` happens to be a reference
     */
    template <typename T>
    const typename std::remove_reference<T>::type *reinterpret_cptr () const;

    /* will copy the contents of the structure out of the log page and
     * advance and align afterwards
     */
    template <typename T>
    T reinterpret_copy_and_add_align ();

    /* equivalent to LOG_READ_ALIGN (safe)
     */
    inline void align ()
    {
      THREAD_ENTRY *const thread_entry = get_thread_entry ();
      assert (thread_entry == &cubthread::get_entry ());
      LOG_READ_ALIGN (thread_entry, &m_lsa, m_page, LOG_CS_SAFE_READER);
    }

    /* equivalent to LOG_READ_ADD_ALIGN (safe)
     */
    inline void add_align (size_t size)
    {
      THREAD_ENTRY *const thread_entry = get_thread_entry ();
      assert (thread_entry == &cubthread::get_entry ());
      LOG_READ_ADD_ALIGN (thread_entry, size, &m_lsa, m_page, LOG_CS_SAFE_READER);
    }

    template <typename T>
    void add_align ();

    /* equivalent to LOG_READ_ADVANCE_WHEN_DOESNT_FIT (safe)
     */
    inline void advance_when_does_not_fit (size_t size)
    {
      THREAD_ENTRY *const thread_entry = get_thread_entry ();
      assert (thread_entry == &cubthread::get_entry ());
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_entry, size, &m_lsa, m_page, LOG_CS_SAFE_READER);
    }

    /* returns whether the supplied lengths is contained in the currently
     * loaded log page (also considering the current offset)
     */
    inline bool does_fit_in_current_page (size_t size) const
    {
      return (m_lsa.offset + static_cast<int> (size) < LOGAREA_SIZE);
    }

    /* copy from log into externally supplied buffer
     * also advancing the - internally kept - page pointer if needed
     */
    inline void copy_from_log (char *dest, size_t length)
    {
      THREAD_ENTRY *const thread_entry = get_thread_entry ();
      assert (thread_entry == &cubthread::get_entry ());
      // will also advance log page if needed
      logpb_copy_from_log (thread_entry, dest, length, &m_lsa, m_page);
    }

    /*
     * TODO: somehow this function, add_align and advance_when_does_not_fit
     * have the same core functionality and could be combined
     */
    inline int skip (size_t size)
    {
      int temp_length = static_cast<int> (size);

      if (m_lsa.offset + temp_length < static_cast<int> (LOGAREA_SIZE))
	{
	  m_lsa.offset += temp_length;
	}
      else
	{
	  THREAD_ENTRY *const thread_entry = get_thread_entry ();
	  assert (thread_entry == &cubthread::get_entry ());
	  while (temp_length > 0)
	    {
	      if (m_lsa.offset + temp_length >= static_cast<int> (LOGAREA_SIZE))
		{
		  temp_length -= static_cast<int> (LOGAREA_SIZE) - static_cast<int> (m_lsa.offset);

		  ++m_lsa.pageid;

		  LOG_LSA fetch_lsa;
		  fetch_lsa.pageid = m_lsa.pageid;
		  fetch_lsa.offset = LOG_PAGESIZE;

		  if (const auto err_fetch_page = fetch_page (thread_entry) != NO_ERROR)
		    {
		      return err_fetch_page;
		    }
		  // in the newly retrieved page, we're back to square zero
		  m_lsa.offset = 0;

		  align ();
		}
	      else
		{
		  m_lsa.offset += temp_length;
		  temp_length = 0;
		}
	    }
	}

      return NO_ERROR;
    }

  private:
    inline const char *get_cptr () const
    {
      assert (!m_lsa.is_null ());
      return m_page->area + m_lsa.offset;
    }

    inline int fetch_page (THREAD_ENTRY *const thread_p)
    {
      if (logpb_fetch_page (thread_p, &m_lsa, LOG_CS_SAFE_READER, m_page) != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_reader::fetch_page");
	  return ER_FAILED;
	}

      return NO_ERROR;
    }
    inline THREAD_ENTRY *get_thread_entry ()
    {
      if (m_thread_entry == nullptr)
	{
	  m_thread_entry = &cubthread::get_entry ();
	}
      return m_thread_entry;
    }

  private:
    /* internally cached thread entry;
     * assumption is that the entire execution happens in the same thread
     */
    THREAD_ENTRY *m_thread_entry = nullptr;
    log_lsa m_lsa = NULL_LSA;
    log_page *m_page;
    char m_area_buffer[IO_MAX_PAGE_SIZE + DOUBLE_ALIGNMENT];
};

inline void
LOG_READ_ALIGN (THREAD_ENTRY *thread_p, LOG_LSA *lsa, LOG_PAGE *log_pgptr,
		log_cs_access_mode cs_access_mode)
{
  lsa->offset = DB_ALIGN (lsa->offset, DOUBLE_ALIGNMENT);
  while (lsa->offset >= (int) LOGAREA_SIZE)
    {
      assert (log_pgptr != NULL);
      lsa->pageid++;
      if (logpb_fetch_page (thread_p, lsa, cs_access_mode, log_pgptr) != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "LOG_READ_ALIGN (log_cs_access_mode: %d)",
			     (int)cs_access_mode);
	}
      lsa->offset -= LOGAREA_SIZE;
      lsa->offset = DB_ALIGN (lsa->offset, DOUBLE_ALIGNMENT);
    }
}
inline void
LOG_READ_ADD_ALIGN (THREAD_ENTRY *thread_p, size_t add, LOG_LSA *lsa, LOG_PAGE *log_pgptr,
		    log_cs_access_mode cs_access_mode)
{
  lsa->offset += add;
  LOG_READ_ALIGN (thread_p, lsa, log_pgptr, cs_access_mode);
}
inline void
LOG_READ_ADVANCE_WHEN_DOESNT_FIT (THREAD_ENTRY *thread_p, size_t length, LOG_LSA *lsa,
				  LOG_PAGE *log_pgptr,
				  log_cs_access_mode cs_access_mode)
{
  if (lsa->offset + static_cast < int > (length) >= static_cast < int > (LOGAREA_SIZE))
    {
      assert (log_pgptr != NULL);
      lsa->pageid++;
      if (logpb_fetch_page (thread_p, lsa, cs_access_mode, log_pgptr) != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "LOG_READ_ADVANCE_WHEN_DOESNT_FIT (log_cs_access_mode: %d)",
			     (int)cs_access_mode);
	}
      lsa->offset = 0;
    }
}


/* implementation
 */

template <typename T>
const typename std::remove_reference<T>::type *log_reader::reinterpret_cptr () const
{
  using rem_ref_t = typename std::remove_reference<T>::type;
  const rem_ref_t *p = reinterpret_cast<const rem_ref_t *> (get_cptr ());
  return p;
}

template <typename T>
T log_reader::reinterpret_copy_and_add_align ()
{
  T data;
  constexpr auto size_of_t = sizeof (T);
  memcpy (&data, get_cptr (), size_of_t);
  add_align (size_of_t);
  // compiler's NRVO will hopefully kick in here and optimize this away
  return data;
}

template <typename T>
void log_reader::add_align ()
{
  const int type_size = sizeof (T);
  add_align (type_size);
}

#endif // LOG_READER_HPP
