/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

//
// parser_allocator.hpp - C++ extensions to parser allocation
//

#ifndef _PARSER_ALLOCATOR_HPP_
#define _PARSER_ALLOCATOR_HPP_

#include "mem_block.hpp"

// forward def
struct parser_context;

class parser_block_allocator : public cubmem::block_allocator
{
  public:
    parser_block_allocator () = delete;
    parser_block_allocator (parser_context *parser);

  private:
    void alloc (cubmem::block &b, size_t size);
    void dealloc (cubmem::block &b);

    parser_context *m_parser;
};

#endif // _PARSER_ALLOCATOR_HPP_
