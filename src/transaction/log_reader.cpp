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

#include "log_reader.hpp"

#include "log_impl.h"
#include "thread_manager.hpp"

log_reader::log_reader ()
{
  m_page = reinterpret_cast<log_page *> (PTR_ALIGN (m_area_buffer, MAX_ALIGNMENT));
}

int log_reader::set_lsa_and_fetch_page (const log_lsa &lsa, fetch_mode fetch_page_mode)
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
  THREAD_ENTRY *const thread_entry = get_thread_entry ();
  assert (thread_entry == &cubthread::get_entry ());
  LOG_READ_ALIGN (thread_entry, &m_lsa, m_page, LOG_CS_SAFE_READER);
}

void log_reader::add_align (size_t size)
{
  THREAD_ENTRY *const thread_entry = get_thread_entry ();
  assert (thread_entry == &cubthread::get_entry ());
  LOG_READ_ADD_ALIGN (thread_entry, size, &m_lsa, m_page, LOG_CS_SAFE_READER);
}

void log_reader::advance_when_does_not_fit (size_t size)
{
  THREAD_ENTRY *const thread_entry = get_thread_entry ();
  assert (thread_entry == &cubthread::get_entry ());
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_entry, size, &m_lsa, m_page, LOG_CS_SAFE_READER);
}

bool log_reader::does_fit_in_current_page (size_t size) const
{
  return (m_lsa.offset + static_cast<int> (size) < LOGAREA_SIZE);
}

void log_reader::copy_from_log (char *dest, size_t length)
{
  THREAD_ENTRY *const thread_entry = get_thread_entry ();
  assert (thread_entry == &cubthread::get_entry ());
  // will also advance log page if needed
  logpb_copy_from_log (thread_entry, dest, length, &m_lsa, m_page);
}

const char *log_reader::get_cptr () const
{
  assert (!m_lsa.is_null ());
  return m_page->area + m_lsa.offset;
}

int log_reader::skip (size_t size)
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

int log_reader::fetch_page (THREAD_ENTRY *const thread_p)
{
  if (logpb_fetch_page (thread_p, &m_lsa, LOG_CS_SAFE_READER, m_page) != NO_ERROR)
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


void
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

void
LOG_READ_ADD_ALIGN (THREAD_ENTRY *thread_p, size_t add, LOG_LSA *lsa, LOG_PAGE *log_pgptr,
		    log_cs_access_mode cs_access_mode)
{
  lsa->offset += add;
  LOG_READ_ALIGN (thread_p, lsa, log_pgptr, cs_access_mode);
}

void
LOG_READ_ADVANCE_WHEN_DOESNT_FIT (THREAD_ENTRY *thread_p, size_t length, LOG_LSA *lsa,
				  LOG_PAGE *log_pgptr, log_cs_access_mode cs_access_mode)
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
