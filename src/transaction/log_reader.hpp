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

#include "log_lsa.hpp"
#include "log_impl.h"

#include <type_traits>

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
    inline log_reader (LOG_CS_ACCESS_MODE cs_access);
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

    inline int set_lsa_and_fetch_page (const log_lsa &lsa, fetch_mode fetch_page_mode = fetch_mode::NORMAL);
    inline const log_hdrpage &get_page_header () const;

    inline const log_page *get_page () const;

    /*
     * Note: `remove_reference` helps if function is called with a typedef
     * that is actually a reference
     * eg:
     *    ..reinterpret_cptr<DUMMY_TYPE>()
     * where `DUMMY_TYPE` happens to be a reference
     */
    template <typename T>
    inline const typename std::remove_reference<T>::type *reinterpret_cptr () const;

    /* will copy the contents of the structure out of the log page and
     * advance and align afterwards
     */
    template <typename T>
    inline T reinterpret_copy_and_add_align ();

    template <typename T>
    inline void reinterpret_copy_and_add_align (T &t);

    /* equivalent to LOG_READ_ALIGN
     */
    inline void align ();

    /* equivalent to LOG_READ_ADD_ALIGN
     */
    inline void add_align (size_t size);

    template <typename T>
    inline void add_align ();

    /* equivalent to LOG_READ_ADVANCE_WHEN_DOESNT_FIT
     */
    inline void advance_when_does_not_fit (size_t size);

    /* returns whether the supplied lengths is contained in the currently
     * loaded log page (also considering the current offset)
     */
    inline bool does_fit_in_current_page (size_t size) const;

    /* copy from log into externally supplied buffer
     * also advancing the - internally kept - page pointer if needed
     */
    inline void copy_from_log (char *dest, size_t length);

    /*
     * TODO: somehow this function, add_align and advance_when_does_not_fit
     * have the same core functionality and could be combined
     */
    inline int skip (size_t size);

  private:
    inline const char *get_cptr () const;

    inline int fetch_page (THREAD_ENTRY *const thread_p);
    inline THREAD_ENTRY *get_thread_entry ();

  private:
    /* internally cached thread entry;
     * assumption is that the entire execution happens in the same thread
     */
    THREAD_ENTRY *m_thread_entry = nullptr;
    log_lsa m_lsa = NULL_LSA;
    LOG_CS_ACCESS_MODE m_cs_access = LOG_CS_FORCE_USE;
    log_page *m_page = nullptr;
    char m_area_buffer[IO_MAX_PAGE_SIZE + DOUBLE_ALIGNMENT];
};

inline void LOG_READ_ALIGN (THREAD_ENTRY *thread_p, LOG_LSA *lsa, LOG_PAGE *log_pgptr,
			    LOG_CS_ACCESS_MODE cs_access_mode = LOG_CS_FORCE_USE);
inline void LOG_READ_ADD_ALIGN (THREAD_ENTRY *thread_p, size_t add, LOG_LSA *lsa, LOG_PAGE *log_pgptr,
				LOG_CS_ACCESS_MODE cs_access_mode = LOG_CS_FORCE_USE);
inline void LOG_READ_ADVANCE_WHEN_DOESNT_FIT (THREAD_ENTRY *thread_p, size_t length, LOG_LSA *lsa,
    LOG_PAGE *log_pgptr, LOG_CS_ACCESS_MODE cs_access_mode = LOG_CS_FORCE_USE);


/* implementation
 */
#include "thread_manager.hpp"

#include <cstring>

log_reader::log_reader (LOG_CS_ACCESS_MODE cs_access)
  : m_cs_access (cs_access)
{
  m_page = reinterpret_cast<log_page *> (PTR_ALIGN (m_area_buffer, MAX_ALIGNMENT));
}

int log_reader::set_lsa_and_fetch_page (const log_lsa &lsa, fetch_mode fetch_page_mode)
{
  const bool do_fetch_page { fetch_page_mode == fetch_mode::FORCE || m_lsa.pageid != lsa.pageid };
  m_lsa = lsa;
  if (do_fetch_page)
    {
      THREAD_ENTRY *const thread_p = get_thread_entry ();
      assert (thread_p == &cubthread::get_entry ());
      return fetch_page (thread_p);
    }
  return NO_ERROR;
}

const log_hdrpage &log_reader::get_page_header () const
{
  return m_page->hdr;
}

const log_page *log_reader::get_page () const
{
  return m_page;
}

void log_reader::align ()
{
  THREAD_ENTRY *const thread_p = get_thread_entry ();
  assert (thread_p == &cubthread::get_entry ());
  LOG_READ_ALIGN (thread_p, &m_lsa, m_page);
}

void log_reader::add_align (size_t size)
{
  THREAD_ENTRY *const thread_p = get_thread_entry ();
  assert (thread_p == &cubthread::get_entry ());
  LOG_READ_ADD_ALIGN (thread_p, size, &m_lsa, m_page);
}

void log_reader::advance_when_does_not_fit (size_t size)
{
  THREAD_ENTRY *const thread_p = get_thread_entry ();
  assert (thread_p == &cubthread::get_entry ());
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, size, &m_lsa, m_page);
}

bool log_reader::does_fit_in_current_page (size_t size) const
{
  return (m_lsa.offset + static_cast<int> (size) < LOGAREA_SIZE);
}

void log_reader::copy_from_log (char *dest, size_t length)
{
  THREAD_ENTRY *const thread_p = get_thread_entry ();
  assert (thread_p == &cubthread::get_entry ());
  // will also advance log page if needed
  logpb_copy_from_log (thread_p, dest, static_cast<int> (length), &m_lsa, m_page);
}

const char *log_reader::get_cptr () const
{
  assert (!m_lsa.is_null ());
  return m_page->area + m_lsa.offset;
}

int log_reader::skip (size_t size)
{
  THREAD_ENTRY *const thread_p = get_thread_entry ();
  assert (thread_p == &cubthread::get_entry ());
  int temp_length = static_cast<int> (size);

  if (m_lsa.offset + temp_length < static_cast<int> (LOGAREA_SIZE))
    {
      m_lsa.offset += temp_length;
    }
  else
    {
      while (temp_length > 0)
	{
	  if (m_lsa.offset + temp_length >= static_cast<int> (LOGAREA_SIZE))
	    {
	      temp_length -= static_cast<int> (LOGAREA_SIZE) - static_cast<int> (m_lsa.offset);

	      ++m_lsa.pageid;

	      LOG_LSA fetch_lsa;
	      fetch_lsa.pageid = m_lsa.pageid;
	      fetch_lsa.offset = LOG_PAGESIZE;

	      if (const auto err_fetch_page = fetch_page (thread_p) != NO_ERROR)
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

int log_reader::fetch_page (THREAD_ENTRY *const thread_p)
{
  if (logpb_fetch_page (thread_p, &m_lsa, m_cs_access, m_page) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_reader::fetch_page");
      return ER_FAILED;
    }

  return NO_ERROR;
}

THREAD_ENTRY *log_reader::get_thread_entry ()
{
  if (m_thread_entry == nullptr)
    {
      m_thread_entry = &cubthread::get_entry ();
    }
  return m_thread_entry;
}

template <typename T>
inline const typename std::remove_reference<T>::type *log_reader::reinterpret_cptr () const
{
  using rem_ref_t = typename std::remove_reference<T>::type;
  const rem_ref_t *p = reinterpret_cast<const rem_ref_t *> (get_cptr ());
  return p;
}

template <typename T>
inline T log_reader::reinterpret_copy_and_add_align ()
{
  T data;
  reinterpret_copy_and_add_align (data);
  // compiler's NRVO will hopefully kick in here and optimize this away
  return data;
}

template <typename T>
inline void log_reader::reinterpret_copy_and_add_align (T &t)
{
  constexpr size_t size_of_t = sizeof (T);
  std::memcpy (&t, get_cptr (), size_of_t);
  add_align (size_of_t);
}

template <typename T>
void log_reader::add_align ()
{
  const int type_size = sizeof (T);
  add_align (type_size);
}

void LOG_READ_ALIGN (THREAD_ENTRY *thread_p, LOG_LSA *lsa, LOG_PAGE *log_pgptr, LOG_CS_ACCESS_MODE cs_access_mode)
{
  lsa->offset = DB_ALIGN (lsa->offset, DOUBLE_ALIGNMENT);
  while (lsa->offset >= (int) LOGAREA_SIZE)
    {
      assert (log_pgptr != NULL);
      lsa->pageid++;
      if (logpb_fetch_page (thread_p, lsa, cs_access_mode, log_pgptr) != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "LOG_READ_ALIGN");
	}
      lsa->offset -= LOGAREA_SIZE;
      lsa->offset = DB_ALIGN (lsa->offset, DOUBLE_ALIGNMENT);
    }
}

void LOG_READ_ADD_ALIGN (THREAD_ENTRY *thread_p, size_t add, LOG_LSA *lsa, LOG_PAGE *log_pgptr,
			 LOG_CS_ACCESS_MODE cs_access_mode)
{
  lsa->offset += add;
  LOG_READ_ALIGN (thread_p, lsa, log_pgptr, cs_access_mode);
}

void LOG_READ_ADVANCE_WHEN_DOESNT_FIT (THREAD_ENTRY *thread_p, size_t length, LOG_LSA *lsa, LOG_PAGE *log_pgptr,
				       LOG_CS_ACCESS_MODE cs_access_mode)
{
  if (lsa->offset + static_cast<int> (length) >= static_cast<int> (LOGAREA_SIZE))
    {
      assert (log_pgptr != NULL);
      lsa->pageid++;
      if (logpb_fetch_page (thread_p, lsa, cs_access_mode, log_pgptr) != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "LOG_READ_ADVANCE_WHEN_DOESNT_FIT");
	}
      lsa->offset = 0;
    }
}

#endif // LOG_READER_HPP
