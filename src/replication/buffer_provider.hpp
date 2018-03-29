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

/*
 * buffer_provider.hpp
 */

#ident "$Id$"

#ifndef _BUFFER_PROVIDER_HPP_
#define _BUFFER_PROVIDER_HPP_

#include "error_code.h"
#include "stream_common.hpp"
#include <vector>
#include <cstddef>

class packing_stream_buffer;
class packing_stream;

/*
 * an object of this type provides buffers (memory)
 */
class buffer_provider : public pinner
{
private:
  /* a buffer provider may allocate several buffers */
  std::vector<packing_stream_buffer*> m_buffers;

  static buffer_provider *default_buffer_provider;

protected:
  size_t min_alloc_size;
  size_t max_alloc_size;

public:

  buffer_provider () { min_alloc_size = 512 * 1024; max_alloc_size = 100 * 1024 * 1024; };

  ~buffer_provider ();

  virtual int allocate_buffer (packing_stream_buffer **new_buffer, const size_t &amount);

  virtual int free_all_buffers (void);
    
  virtual int extend_buffer (packing_stream_buffer **existing_buffer, const size_t &amount);

  virtual int add_buffer (packing_stream_buffer *new_buffer);

  static buffer_provider *get_default_instance (void);
};


#endif /* _BUFFER_PROVIDER_HPP_ */
