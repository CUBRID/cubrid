#include "Al_AffixAllocator.hpp"
#include "Al_StackAllocator.hpp"
#include "StrBuf.hpp"
#include <assert.h>
#ifdef __linux__
#include <stddef.h>//size_t on Linux
#endif
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 8192

//--------------------------------------------------------------------------------
#define ERR(format, ...) printf("ERR " format "\n", __VA_ARGS__)
#define WRN(format, ...) printf("WRN " format "\n", __VA_ARGS__)

//--------------------------------------------------------------------------------
struct Prefix
{
  char buf[66];
  Prefix() { memset(buf, 0xBB, sizeof(buf)); }
};
struct Suffix
{
  char buf[64];
  Suffix() { memset(buf, 0xEE, sizeof(buf)); }
};

//--------------------------------------------------------------------------------
class Test
{
private:
  char                                                   _buf[sizeof(Prefix) + N + sizeof(Suffix)];// working buffer
  size_t                                                 _dim;                                     //_ref[_dim]
  size_t                                                 _len;                                     // sizeof(_ref)
  char*                                                  _ref;                                     // reference buffer
  Al::StackAllocator                                     _stackAllocator;
  Al::AffixAllocator<Al::StackAllocator, Prefix, Suffix> _affixAllocator;
  Al::Blk                                                _blk;
  StrBuf                                                 _sb;

public:
  Test()
    : _buf()
    , _dim(1024)
    , _len(0)
    , _ref((char*)calloc(_dim, 1))
    , _stackAllocator(_buf, sizeof(_buf))
    , _affixAllocator(_stackAllocator)
    , _blk()
    , _sb()
  {
  }

  void operator()(size_t size)
  {// prepare for a test with a buffer of <size> bytes
    if(_dim < size)
      {// adjust internal buffer is necessary
        do
          {
            _dim *= 2;
          }
        while(_dim < size);
        char* p = (char*)malloc(_dim);
      }
    _len = 0;
    _stackAllocator.~StackAllocator();
    _blk = _affixAllocator.allocate(size);
    _sb.set(size, _blk.ptr);
  }

  template<size_t Size, typename... Args>
  void operator()(const char* file, int line, const char (&format)[Size], Args&&... args)
  {
    int len = snprintf(_ref + _len, _len < _blk.dim ? _blk.dim - _len : 0, format, args...);
    if(len < 0)
      {
        ERR("[%s(%d)] StrBuf([%zu]) snprintf()=%d", file, line, _blk.dim, len);
        return;
      }
    else
      {
        _len += len;
      }
    _sb(format, args...);
    if(_sb.len() != _len)
      {
        ERR("[%s(%d)] StrBuf([%zu]) len()=%zu expect %zu", file, line, _blk.dim, _sb.len(), _len);
        return;
      }
    if(strcmp(_sb, _ref))
      {
        ERR("[%s(%d)] StrBuf([%zu]) {\"%s\"} expect{\"%s\"}", file, line, _blk.dim, (const char*)_sb, _ref);
        return;
      }
    if(_affixAllocator.check(_blk))
      {
        ERR("[%s(%d)] StrBuf(buf[%zu]) memory corruption", file, line, _blk.dim);
        return;
      }
  }

  void operator()(const char* file, int line, char ch)
  {
    if(_len + 1 < _blk.dim)
      {// include also '\0'
        _ref[_len]     = ch;
        _ref[_len + 1] = '\0';
      }
    ++_len;

    _sb += ch;
    if(strcmp((const char*)_sb, _ref))
      {
        ERR("[%s(%d)] StrBuf([%zu]) {\"%s\"} expect {\"%s\"}", file, line, _blk.dim, (const char*)_sb, _ref);
        return;
      }
    if(_affixAllocator.check(_blk))
      {
        ERR("[%s(%d)] StrBuf([%zu]) memory corruption", file, line, _blk.dim);
      }
  }
};
Test tst;

#define SB_FORMAT(format, ...) tst(__FILE__, __LINE__, format, ##__VA_ARGS__)
#define SB_CHAR(ch) tst(__FILE__, __LINE__, ch);

//================================================================================
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
      tst(n);
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