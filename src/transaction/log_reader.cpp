
#include "log_recovery_util.hpp"
#include "log_reader.hpp"
#include "thread_manager.hpp"

log_reader::log_reader ()
{
  m_page = reinterpret_cast<log_page *> (PTR_ALIGN (m_area_buffer, MAX_ALIGNMENT));
}

const log_lsa &log_reader::get_lsa() const
{
  return m_lsa;
}

int log_reader::set_lsa_and_fetch_page (const log_lsa &lsa)
{
  m_lsa = lsa;
  return fetch_page();
}

const log_hdrpage &log_reader::get_page_header() const
{
  return m_page->hdr;
}

//const log_lsa &log_reader::position_on_first_record()
//{
//  m_lsa.offset = get_page_header().offset;
//  return m_lsa;
//}

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

	      if (logpb_fetch_page (thread_p, &fetch_lsa, LOG_CS_FORCE_USE, m_page) != NO_ERROR)
		{
		  // TODO: do this outside
		  // LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);

		  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_reader::advance");
		  return ER_FAILED;
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

void log_reader::assert_equals (const log_lsa &other_log_lsa, const log_page &other_log_page) const
{
  assert (this->m_lsa == other_log_lsa);
  assert (this->m_page->hdr == other_log_page.hdr);
  const auto res = strncmp (static_cast<const char *> (m_page->area), static_cast<const char *> (other_log_page.area),
			    LOGAREA_SIZE - 1);
  assert (res == 0);
}

int log_reader::fetch_page ()
{
  THREAD_ENTRY *thread_p = &cubthread::get_entry ();
  if (logpb_fetch_page (thread_p, &m_lsa, LOG_CS_FORCE_USE, m_page) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_reader::fetch_page");
      return ER_FAILED;
    }

  return NO_ERROR;
}
