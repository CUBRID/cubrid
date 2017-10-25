#include "string_buffer.hpp"
#include <memory.h>

string_buffer::string_buffer(size_t capacity, char* buffer)
  : m_buf(buffer)
  , m_dim(capacity)
  , m_len(0)
{
  if(buffer)
    m_buf[0] = '\0';
}

void string_buffer::set(size_t capacity, char* buffer)
{// associate with a new buffer[capacity]
  m_buf = buffer;
  m_dim = capacity;
  if(m_buf)
    {
      m_buf[0] = '\0';
    }
  m_len = 0;
}

void string_buffer::operator()(size_t len, void* bytes)
{// add "len" bytes to internal buffer; "bytes" can have '\0' in the middle
  if(bytes && m_len + len < m_dim)
    {
      memcpy(m_buf + m_len, bytes, len);
      m_buf[m_len += len] = 0;
    }
  else
    {
      m_len += len;
    }
}

void string_buffer::operator+=(const char ch)
{
  if(m_len + 1 < m_dim)
    {// include also '\0'
      m_buf[m_len]     = ch;
      m_buf[m_len + 1] = '\0';
    }
  ++m_len;
}