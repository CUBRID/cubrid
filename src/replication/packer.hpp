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
 * packer.hpp
 */

#ident "$Id$"

#ifndef _PACKER_HPP_
#define _PACKER_HPP_

#include "common_utils.hpp"
#include "object_representation.h"

#include <vector>

/* 
 * the packer object packs or unpacks promitive objects from/into a buffer 
 * the buffer is provided at initialization
 * each object is atomically packed into the buffer.
 * (atomically == means no other object could insert sub-objects in the midle of the packing of 
 * currently serialized object)
 */
class packer
{
public:
  /* method for starting a packing context */
  packer (void) { m_ptr = NULL;};
  packer (BUFFER_UNIT *storage, const size_t amount);

  int init (BUFFER_UNIT *storage, const size_t amount);

  int pack_int (const int value);
  int unpack_int (int *value);
  int peek_unpack_int (int *value);

  int pack_bigint (const DB_BIGINT &value);
  int unpack_bigint (DB_BIGINT *value);

  int pack_int_array (const int *array, const int count);
  int unpack_int_array (int *array, int &count);

  /* TODO[arnia] : remove these if not needed */
  int pack_int_vector (const std::vector<int> &array);
  int unpack_int_vector (std::vector <int> &array);

  int pack_db_value (const DB_VALUE &value);
  int unpack_db_value (DB_VALUE *value);

  int pack_small_string (const char *string);
  int unpack_small_string (char *string, const size_t max_size);

  BUFFER_UNIT *get_curr_ptr (void) { return m_ptr; };

private:
  BUFFER_UNIT *m_ptr;       /* current pointer of serialization */
  BUFFER_UNIT *m_end_ptr;     /* end of avaialable serialization scope */
};


#endif /* _REPLICATION_SERIALIZATION_HPP_ */
