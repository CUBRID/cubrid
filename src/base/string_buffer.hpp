#pragma once
#ifdef __linux__
#include <stddef.h>//size_t on Linux
#endif
#include <stdio.h>
#if 0//!!! snprintf() != _sprintf_p() when buffer capacity is exceeded !!! (disabled until further testing)
#ifdef _WIN32
#define snprintf                                                                                                       \
  _sprintf_p// snprintf() on Windows doesn't support positional parms but there is a similar function; snprintf_p();   \
            // on Linux supports positional parms by default
#endif
#endif

class string_buffer//collect formatted text (printf-like syntax)
{             
  char*  m_buf;// pointer to a memory buffer (not owned)
  size_t m_dim;// dimension|capacity of the buffer
  size_t m_len;// current length of the buffer content
public:
  string_buffer(size_t capacity = 0, char* buffer = 0);

  operator const char*() { return m_buf; }

  size_t len() { return m_len; }
  void   clr() { m_buf[m_len = 0] = '\0'; }

  void set(size_t capacity, char* buffer);// associate with a new buffer[capacity]

  void operator()(size_t len, void* bytes);// add "len" bytes to internal buffer; "bytes" can have '\0' in the middle
  void operator+=(const char ch);

  template<size_t Size, typename... Args> void operator()(const char (&format)[Size], Args&&... args)
  {
    int len = snprintf(m_buf + m_len, m_len < m_dim ? m_dim - m_len : 0, format, args...);
    if(len >= 0)
      {
        if(m_dim <= m_len + len)
          {
            // WRN not enough space in buffer
          }
        m_len += len;
      }
  }
};