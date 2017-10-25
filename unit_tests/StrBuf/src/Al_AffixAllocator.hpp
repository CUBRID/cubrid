#if 0//Affix Allocator: allocate(sizeof(Prefix) + size + sizeof(Suffix))
- usefull for debug, statistics, information, ...
    - use Prefix & Suffix as fences to detect wrong writes: "BBBBBBBB"..."EEEEEEEE"
    - use Prefix & Suffix for human readable text to ease memory reading: "type=MyClass"..."end of type=MyClass"
    - use Prefix & Suffix to store information & statistics (creation timestamp, source code file & line, access count, ...)
USAGE:
#endif
#pragma once
#include "Al_Blk.hpp"
#ifdef __linux__
#include <stddef.h>//size_t on Linux
#endif
#include <new>

namespace Al
{
  template<typename Allocator, typename Prefix, typename Suffix> class AffixAllocator
  {
  private:
    static const size_t m_pfxLen = sizeof(Prefix);
    static const size_t m_sfxLen = sizeof(Suffix);
    Allocator&          m_a;

  public:
    AffixAllocator(Allocator& allocator)
      : m_a(allocator)
    {
    }

    Blk allocate(size_t size)
    {
      Blk blk = m_a.allocate(m_pfxLen + size + m_sfxLen);
      if(!blk)
        return {0, 0};
      new(blk.ptr) Prefix;                 //placement new to initialize Prefix memory
      new(blk.ptr + m_pfxLen + size) Suffix;//placement new to initialize Suffix memory
      return {size, blk.ptr + m_pfxLen};
    }

    void deallocate(Blk blk)
    {
      //check if Prefix & Suffix are unchanged!
      //...
      _a.deallocate({m_pfxLen + blk.dim + m_sfxLen, blk.ptr - m_pfxLen});
    }

    unsigned check(Blk blk)
    {
      Prefix pfx;
      Suffix sfx;
      int    err = 0;
      err |= (memcmp(&pfx, blk.ptr - m_pfxLen, m_pfxLen)) ? 1 : 0;
      err |= (memcmp(&sfx, blk.ptr + blk.dim, m_sfxLen)) ? 2 : 0;
      return err;
    }
  };
}// namespace Al

#if 0//using inheritance
template<typename Allocator, typename Prefix, typename Suffix=void> class AffixAllocator
    : Allocator
{
public:
    void* allocate(size_t size){
        void* ptr = Allocator::allocate(sizeof(Prefix) + size + sizeof(Suffix));
        new(ptr) Prefix;                    //placement new to initialize Prefix memory 
        new(ptr+sizeof(Prefix)+size) Suffix;//placement new to initialize Suffix memory 
        return ptr;
    }

    void deallocate(void* ptr, size_t size){
        //check if Prefix & Suffix are unchanged!
        //...
        Allocator::deallocate(sizeof(Prefix) + size + sizeof(Suffix));
    }
};
#endif
