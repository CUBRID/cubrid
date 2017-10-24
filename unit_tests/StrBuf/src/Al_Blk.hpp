#pragma once
#ifdef __linux__
#include <stddef.h> //size_t on Linux
#endif

namespace Al{//Allocator
    struct Blk{
        size_t  dim;    //size of the memory block pointed by ptr
        char*   ptr;    //pointer to a memory block

        Blk(size_t dim=0, void* ptr=0)
            : dim(dim)
            , ptr((char*)ptr)
        {}

        operator bool(){
            return (dim && ptr);
        }

        friend bool operator==(Blk blk0, Blk blk1){
            return (blk0.dim==blk1.dim && blk0.ptr==blk1.ptr);
        }

        friend bool operator!=(Blk blk0, Blk blk1){
            return (blk0.dim!=blk1.dim || blk0.ptr!=blk1.ptr);
        }
    };
}