#include "file_io.h"

#include "page_buffer.h"

data_page_owner::data_page_owner (const char *data_page_start, size_t size)
  : m_data_page_string (data_page_start, size)
{
  std::memcpy (m_pgptr, m_data_page_string.c_str (), size);
  pgbuf_cast_pgptr_to_iopgptr (m_pgptr, m_io_pgptr);
}

data_page_owner::~data_page_owner ()
{
  delete[] m_pgptr; // TODO: see if you can get your hands on a context for pgbuf_unfix.
}

const FILEIO_PAGE_RESERVED &
data_page_owner::prv ()
{
  return m_io_pgptr->prv;
}