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
// method_package.hpp
//

#ifndef _METHOD_PACKAGE_HPP_
#define _METHOD_PACKAGE_HPP_

#ident "$Id$"

#include "dbtype_def.h"
#include "string_buffer.hpp"
#include "session.h"

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

/* built-in packages */
class dbms_output
{
    public:
        static void enable (cubthread::entry * thread_p, size_t size)
        {
          string_buffer* buffer = null;

          if (session_get_message_buffer (thread_p, buffer) == NO_ERROR)
          {
            assert (buffer);
            if (buffer->len )
            buffer->
          }
        }

        static void disable (cubthread::entry * thread_p)
        {
          string_buffer* buffer = null;

          if (session_get_message_buffer (thread_p, buffer) == NO_ERROR)
          {
            assert (buffer);
            buffer->clear ();
          }
        }

        static void get_line (DB_OBJECT * target, DB_VALUE * result, DB_VALUE *line, DB_VALUE *status)
        {

        }

        static void put_line (DB_OBJECT * target, DB_VALUE * result, DB_VALUE *item)
        {
            
        }
};

namespace cubmethod
{

}		// namespace cubmethod

#endif				/* _METHOD_PACKAGE_HPP_ */
