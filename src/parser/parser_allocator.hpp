/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
