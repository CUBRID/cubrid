/*
 *
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

#ifndef _METHOD_LOB_HANDLER_HPP_
#define _METHOD_LOB_HANDLER_HPP_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <vector>

#include "mem_block.hpp"
#include "method_error.hpp"
#include "method_struct_lob_info.hpp"

namespace cubmethod
{
  /*
   * cubmethod::oid_handler
   *
   * description
   * To support CUBIRD OID (including collection) related functions
   *
   */
  class lob_handler
  {
    public:
      lob_handler ();

      int lob_new (DB_TYPE lob_type, lob_info &info);
      int lob_write (DB_VALUE *lob_dbval, DB_BIGINT offset, cubmem::block &blk, DB_BIGINT &size_written);
      int lob_read (DB_VALUE *lob_dbval, DB_BIGINT offset, int size, cubmem::extensible_block &eb, DB_BIGINT &size_read);

      cubmem::block get_lob_buffer ();

      static const int LOB_MAX_IO_LENGTH = 128 * 1024;

    private:
      cubmem::extensible_block m_lob_buffer;
      bool m_is_buffer_initialized;
  };
} // namespace cubmethod

#endif // _METHOD_LOB_HANDLER_HPP_
