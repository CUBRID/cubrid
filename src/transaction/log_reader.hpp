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

#include <type_traits>

/* encapsulates reading of the log
 *
 * NOTE: not thread safe
 * NOTE: further functionality:
 *  - introduce an internal buffer to be used for copying and serving
 *    data from the log of arbitrary size; currently this is done manually outside this class
 */
class log_reader final
{
  public:
    log_reader ();
    log_reader (log_reader const & ) = delete;
    log_reader (log_reader && ) = delete;

    log_reader &operator = (log_reader const & ) = delete;
    log_reader &operator = (log_reader && ) = delete;

    inline const log_lsa &get_lsa() const
    {
      return m_lsa;
    }

    inline std::int64_t get_pageid() const
    {
      return m_lsa.pageid;
    }

    inline std::int16_t get_offset() const
    {
      return m_lsa.offset;
    }

    int set_lsa_and_fetch_page (const log_lsa &lsa);
    const log_hdrpage &get_page_header() const;

    /*
     * Note: `remove_reference` helps if function is called with a typedef
     * that is actually a reference
     * eg:
     *    ..reinterpret_cptr<DUMMY_TYPE>()
     * where `DUMMY_TYPE` happens to be a reference
     */
    template <typename T>
    const typename std::remove_reference<T>::type *reinterpret_cptr () const;

    /* equivalent to LOG_READ_ALIGN
     */
    void align ();

    /* equivalent to LOG_READ_ADD_ALIGN
     */
    void add_align (size_t size);

    template <typename T>
    void add_align ();

    /* equivalent to LOG_READ_ADVANCE_WHEN_DOESNT_FIT
     */
    void advance_when_does_not_fit (size_t size);

    /* returns whether the supplied lengths is contained in the currently
     * loaded log page (also considering the current offset)
     */
    bool does_fit_in_current_page (size_t size) const;

    /* copy from log into externally supplied buffer
     * also advancing the - internally kept - page pointer if needed
     */
    void copy_from_log (char *dest, size_t length);

    /*
     * TODO: somehow this function, add_align and advance_when_does_not_fit
     * have the same core functionality and could be combined
     */
    int skip (size_t size);

  private:
    const char *get_cptr () const;

    int fetch_page_force_use (THREAD_ENTRY *const thread_p);

  private:
    log_lsa m_lsa = NULL_LSA;
    log_page *m_page;
    char m_area_buffer[IO_MAX_PAGE_SIZE + DOUBLE_ALIGNMENT];
};

void LOG_READ_ALIGN (THREAD_ENTRY *thread_p, LOG_LSA *lsa, LOG_PAGE *log_pgptr);
void LOG_READ_ADD_ALIGN (THREAD_ENTRY *thread_p, size_t add, LOG_LSA *lsa, LOG_PAGE *log_pgptr);
void LOG_READ_ADVANCE_WHEN_DOESNT_FIT (THREAD_ENTRY *thread_p, size_t length, LOG_LSA *lsa,
				       LOG_PAGE *log_pgptr);


/* implementation
 */

template <typename T>
const typename std::remove_reference<T>::type *log_reader::reinterpret_cptr () const
{
  using rem_ref_t = typename std::remove_reference<T>::type;
  const rem_ref_t *p = reinterpret_cast<const rem_ref_t *> (get_cptr());
  return p;
}

template <typename T>
void log_reader::add_align ()
{
  const int type_size = sizeof (T);
  add_align (type_size);
}

#endif // LOG_READER_HPP
