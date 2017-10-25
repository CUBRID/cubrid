#include "Al_AffixAllocator.hpp"
#include "Al_StackAllocator.hpp"
#include "string_buffer.hpp"
#include <assert.h>
#ifdef __linux__
#include <stddef.h>//size_t on Linux
#endif
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 8192

#define ERR(format, ...) printf("ERR " format "\n", __VA_ARGS__)
#define WRN(format, ...) printf("WRN " format "\n", __VA_ARGS__)

struct prefix
{
  char buf[66];
  prefix() { memset(buf, 0xBB, sizeof(buf)); }
};
struct suffix
{
  char buf[64];
  suffix() { memset(buf, 0xEE, sizeof(buf)); }
};

class test_string_buffer
{
private:
  char                                                   m_buf[sizeof(prefix) + N + sizeof(suffix)];// working buffer
  size_t                                                 m_dim;                                     //_ref[_dim]
  size_t                                                 m_len;                                     // sizeof(_ref)
  char*                                                  m_ref;                                     // reference buffer
  Al::StackAllocator                                     m_stackAllocator;
  Al::AffixAllocator<Al::StackAllocator, prefix, suffix> m_affixAllocator;
  Al::Blk                                                m_blk;
  string_buffer                                          m_sb;

public:
  test_string_buffer()
    : m_buf()
    , m_dim(1024)
    , m_len(0)
    , m_ref((char*)calloc(m_dim, 1))
    , m_stackAllocator(m_buf, sizeof(m_buf))
    , m_affixAllocator(m_stackAllocator)
    , m_blk()
    , m_sb()
  {
  }

  void operator()(size_t size)
  {// prepare for a test with a buffer of <size> bytes
    if(m_dim < size)
      {// adjust internal buffer is necessary
        do
          {
            m_dim *= 2;
          }
        while(m_dim < size);
        char* p = (char*)malloc(m_dim);
      }
    m_len = 0;
    m_stackAllocator.~StackAllocator();
    m_blk = m_affixAllocator.allocate(size);
    m_sb.set(size, m_blk.ptr);
  }

  template<size_t Size, typename... Args>
  void operator()(const char* file, int line, const char (&format)[Size], Args&&... args)
  {
    int len = snprintf(m_ref + m_len, m_len < m_blk.dim ? m_blk.dim - m_len : 0, format, args...);
    if(len < 0)
      {
        ERR("[%s(%d)] StrBuf([%zu]) snprintf()=%d", file, line, m_blk.dim, len);
        return;
      }
    else
      {
        m_len += len;
      }
    m_sb(format, args...);
    if(m_sb.len() != m_len)
      {
        ERR("[%s(%d)] StrBuf([%zu]) len()=%zu expect %zu", file, line, m_blk.dim, m_sb.len(), m_len);
        return;
      }
    if(strcmp(m_sb, m_ref))
      {
        ERR("[%s(%d)] StrBuf([%zu]) {\"%s\"} expect{\"%s\"}", file, line, m_blk.dim, (const char*)m_sb, m_ref);
        return;
      }
    if(m_affixAllocator.check(m_blk))
      {
        ERR("[%s(%d)] StrBuf(buf[%zu]) memory corruption", file, line, m_blk.dim);
        return;
      }
  }

  void operator()(const char* file, int line, char ch)
  {
    if(m_len + 1 < m_blk.dim)
      {// include also '\0'
        m_ref[m_len]     = ch;
        m_ref[m_len + 1] = '\0';
      }
    ++m_len;

    m_sb += ch;
    if(strcmp((const char*)m_sb, m_ref))
      {
        ERR("[%s(%d)] StrBuf([%zu]) {\"%s\"} expect {\"%s\"}", file, line, m_blk.dim, (const char*)m_sb, m_ref);
        return;
      }
    if(m_affixAllocator.check(m_blk))
      {
        ERR("[%s(%d)] StrBuf([%zu]) memory corruption", file, line, m_blk.dim);
      }
  }
};
test_string_buffer test;

#define SB_FORMAT(format, ...) test(__FILE__, __LINE__, format, ##__VA_ARGS__)
#define SB_CHAR(ch) test(__FILE__, __LINE__, ch);

int main(int argc, char** argv)
{
  enum Flags
  {
    flDEBUG = (1 << 0),
    flTIME  = (1 << 1)
  };
  unsigned flags = 0;
  for(int i = 1; i < argc; ++i)
    {
      unsigned char* p = (unsigned char*)argv[i];
      unsigned       f = p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];// little endian
      switch(f)
        {
          case 'dbug': flags |= flDEBUG; break;
          case 'time': flags |= flTIME; break;
        }
    }
  if(flags & flDEBUG)
    {
      printf("%s\n", argv[0]);
      for(int i = 1; i < argc; ++i)
        printf("    %s\n", argv[i]);
    }

  auto t0 = std::chrono::high_resolution_clock::now();
  for(size_t n = 1; n < N; ++n)
    {
      test(n);
      SB_FORMAT("simple text.");
      SB_FORMAT("another.");
      SB_CHAR('x');
      SB_CHAR('.');
      SB_FORMAT("format with %d argument.", 1);
      SB_FORMAT("%1$04d-%2$02d-%3$02d.", 1973, 11, 28);
      SB_FORMAT("%3$02d.%2$02d.%1$04d.", 1973, 11, 28);
    }
  auto t1 = std::chrono::high_resolution_clock::now();

  if(flags & flTIME)
    {
      printf("%.9lf ms\n", std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

  return 0;
}