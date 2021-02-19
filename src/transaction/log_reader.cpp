
#include "log_recovery_util.hpp"
#include "log_reader.hpp"
#include "thread_manager.hpp"

log_reader::log_reader ()
{
  m_page = reinterpret_cast<log_page *> (PTR_ALIGN (m_area_buffer, MAX_ALIGNMENT));
}

int log_reader::set_lsa_and_fetch_page (const log_lsa &lsa)
{
  const bool do_fetch_page { m_lsa.pageid != lsa.pageid };
  m_lsa = lsa;
  if (do_fetch_page)
    {
      THREAD_ENTRY *thread_p = &cubthread::get_entry ();
      return fetch_page_force_use (thread_p);
    }
  return NO_ERROR;
}

const log_hdrpage &log_reader::get_page_header() const
{
  return m_page->hdr;
}

void log_reader::add (size_t size)
{
  assert (is_within_current_page (size));
  m_lsa.offset += size;
}

void log_reader::align ()
{
  THREAD_ENTRY *thread_p = &cubthread::get_entry ();
  LOG_READ_ALIGN (thread_p, &m_lsa, m_page);
}

void log_reader::add_align (size_t size)
{
  THREAD_ENTRY *thread_p = &cubthread::get_entry ();
  LOG_READ_ADD_ALIGN (thread_p, size, &m_lsa, m_page);
}

void log_reader::advance_when_does_not_fit (size_t size)
{
  THREAD_ENTRY *thread_p = &cubthread::get_entry ();
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, size, &m_lsa, m_page);
}

bool log_reader::is_within_current_page (size_t size) const
{
  return (m_lsa.offset + static_cast<int> (size) < LOGAREA_SIZE);
}

void log_reader::copy_from_log (char *dest, size_t length)
{
  THREAD_ENTRY *thread_p = &cubthread::get_entry ();
  // will also advance log page if needed
  logpb_copy_from_log (thread_p, dest, length, &m_lsa, m_page);
}

const char *log_reader::get_cptr () const
{
  assert (!m_lsa.is_null ());
  return m_page->area + m_lsa.offset;
}

int log_reader::skip (size_t size)
{
  THREAD_ENTRY *thread_p = &cubthread::get_entry ();
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

	      if (const auto err_fetch_page = fetch_page_force_use (thread_p) != NO_ERROR)
		{
		  return err_fetch_page;
		}
	      // in the newly retrieved page, we're back to square zero
	      m_lsa.offset = 0;

	      align();
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

int log_reader::fetch_page_force_use (THREAD_ENTRY *const thread_p)
{
  if (logpb_fetch_page (thread_p, &m_lsa, LOG_CS_FORCE_USE, m_page) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_reader::fetch_page");
      return ER_FAILED;
    }

  return NO_ERROR;
}
