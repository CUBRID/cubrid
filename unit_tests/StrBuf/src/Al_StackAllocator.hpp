#if 0// Stack Allocator
- allocate sequentially from a memory buffer
- can deallocate only the last allocated block
- usually used with a stack allocated buffer => no sync needed
- constant allocation/dealocation time: O(1)
#endif
#pragma once
#include "Al_Blk.hpp"
#ifdef __linux__
#include <stddef.h>//size_t on Linux
#endif

namespace Al
{
  class StackAllocator
  {
  private:
    char* _ptr;//pointer to a contiguous memory block
    char* _0;  //begin of the available memory (inside given block)
    char* _1;  //end of available memory (=_ptr+SIZE)
  public:
    friend class StackAllocator_Test;//test class can access internals of this class

    StackAllocator(void* buf, size_t size)
      : _ptr((char*)buf)
      , _0(_ptr)
      , _1(_ptr + size)
    {
    }

    ~StackAllocator()
    {//can be called explicitly to reinitialize: a.~StackAllocator();
      _0 = _ptr;
    }

    void operator()(void* buf, size_t size)
    {
      _ptr = (char*)buf;
      _0   = _ptr;
      _1   = _ptr + size;
    }

    Blk allocate(size_t size)
    {
      //round to next aligned?! natural allignment
      //...
      if(_0 + size > _1)
        {//not enough free memory available
          return {0, 0};
        }
      return {size, (_0 += size, _0 - size)};
    }

    void deallocate(Blk blk)
    {
      //assert(owns(blk));
      if((char*)blk.ptr + blk.dim == _0)
        {//last allocated block
          _0 = blk.ptr;
        }
    }

    bool owns(Blk blk)
    {// test if blk in [_0, _1)
      return (_ptr <= blk.ptr && blk.ptr + blk.dim <= _1);
    }

    size_t available() { return (_1 - _0); }

    Blk realloc(Blk blk, size_t size)
    {//fit additional size bytes; extend block if possible
      if(blk.ptr + blk.dim == _0 && blk.ptr + blk.dim + size <= _1)
        {//last allocated block & enough space to extend
          blk.dim += size;
          _0 += size;
          return blk;
        }
      return {0, 0};
    }
  };
}// namespace Al