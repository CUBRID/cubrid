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

/*
 * pinnable_buffer.hpp
 */

#ifndef _PINNABLE_BUFFER_HPP_
#define _PINNABLE_BUFFER_HPP_

#ident "$Id$"

#include "dbtype.h"
#include "pinning.hpp"
#include <atomic>
#include <vector>

/*
 * This should serve as storage for packing / unpacking objects
 * This is not intended to be used as character stream, but as bulk operations: users of it
 * reserve / allocate parts of it; there are objects which deal of byte level operations (see : packer)
 */
namespace cubmem
{

  class pinnable_buffer : public cubbase::pinnable
  {
    public:
      pinnable_buffer ()
      {
	m_storage = NULL;
	m_end_ptr = NULL;
      };

      pinnable_buffer (char *ptr, const size_t buf_size)
      {
	init (ptr, buf_size, NULL);
      };

      ~pinnable_buffer ()
      {
	assert (get_pin_count () == 0);
      };

      char *get_buffer (void)
      {
	return m_storage;
      };

      size_t get_buffer_size (void)
      {
	return m_end_ptr - m_storage;
      };

      int init (char *ptr, const size_t buf_size, cubbase::pinner *referencer);

    protected:

      /* start of allocated memory */
      char *m_storage;
      /* end of allocated memory */
      char *m_end_ptr;
  };

} /* namespace mem */

#endif /* _PINNABLE_BUFFER_HPP_ */
