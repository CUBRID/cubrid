
#include "log_reader.hpp"
#include "thread_manager.hpp"

log_reader::log_reader ()
{
  m_page = reinterpret_cast<log_page *> (PTR_ALIGN (m_area_buffer, MAX_ALIGNMENT));
}

const log_lsa& log_reader::get_lsa() const
{
  return m_lsa;
}

int log_reader::set_lsa (const log_lsa& lsa)
{
  m_lsa = lsa;
  return fetch_page();
}

const log_hdrpage& log_reader::get_page_header() const
{
  return m_page->hdr;
}

int log_reader::fetch_page ()
{
  THREAD_ENTRY* thread_p = &cubthread::get_entry ();
  if (logpb_fetch_page (thread_p, &m_lsa, LOG_CS_FORCE_USE, m_page) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_reader::fetch_page");
      return ER_FAILED;
    }

  return NO_ERROR;
}
