#include "StrBuf.hpp"
#include <memory.h>

//--------------------------------------------------------------------------------
StrBuf::StrBuf(size_t capacity, char* buffer)
    : _buf(buffer)
    , _dim(capacity)
    , _len(0)
{
    if(buffer != nullptr)
        _buf[0] = '\0';
}

//--------------------------------------------------------------------------------
void StrBuf::set(size_t capacity, char* buffer){//associate with a new buffer[capacity]
    _buf = buffer;
    _dim = capacity;
    if(_buf)
        _buf[0] = '\0';
    _len = 0;
}

//--------------------------------------------------------------------------------
void StrBuf::operator()(size_t len, void* bytes) {//add "len" bytes to internal buffer; "bytes" can have '\0' in the middle
    if(bytes && _len+len<_dim){
        memcpy(_buf+_len, bytes, len);
        _buf[_len += len] = 0;
    }else{
        _len += len;
    }
}

//--------------------------------------------------------------------------------
void StrBuf::operator+=(const char ch){
    if(_len+1 < _dim){//include also '\0'
        _buf[_len] = ch;
        _buf[_len+1] = '\0';
    }
    ++_len;
}