#if 0 //Affix Allocator: allocate(sizeof(Prefix) + size + sizeof(Suffix))
- usefull for debug, statistics, information, ...
    - use Prefix & Suffix as fences to detect wrong writes: "BBBBBBBB"..."EEEEEEEE"
    - use Prefix & Suffix for human readable text to ease memory reading: "type=MyClass"..."end of type=MyClass"
    - use Prefix & Suffix to store information & statistics (creation timestamp, source code file & line, access count, ...)
USAGE:
#endif
#pragma once
#include "Al_Blk.hpp"
#ifdef __linux__
#include <stddef.h> //size_t on Linux
#endif
#include <new>

namespace Al{
    template<typename Allocator, typename Prefix, typename Suffix> class AffixAllocator{
    private:
        static constexpr size_t _pfxLen = sizeof(Prefix);
        static constexpr size_t _sfxLen = sizeof(Suffix);
        Allocator& _a;
    public:
        AffixAllocator(Allocator& allocator)
            : _a(allocator)
        {}

        Blk allocate(size_t size){
            Blk blk = _a.allocate(_pfxLen + size + _sfxLen);
            if(!blk)
                return {0, nullptr};
            new(blk.ptr) Prefix;                //placement new to initialize Prefix memory 
            new(blk.ptr+_pfxLen+size) Suffix;   //placement new to initialize Suffix memory 
            return {size, blk.ptr+_pfxLen};
        }

        void deallocate(Blk blk){
            //check if Prefix & Suffix are unchanged!
            //...
            _a.deallocate({_pfxLen+blk.dim+_sfxLen, blk.ptr-_pfxLen});
        }

        unsigned check(Blk blk){
            Prefix pfx;
            Suffix sfx;
            int err = 0;
            err |= (memcmp(&pfx, blk.ptr-_pfxLen, _pfxLen)) ? 1 : 0;
            err |= (memcmp(&sfx, blk.ptr+blk.dim, _sfxLen)) ? 2 : 0;
            return err;
        }
    };
}

#if 0 //using inheritance
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