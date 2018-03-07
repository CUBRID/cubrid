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

  void align (const size_t req_alignement) { m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, req_alignement); };
  size_t get_packed_int_size (size_t curr_offset);
  int pack_int (const int value);
  int unpack_int (int *value);
  int peek_unpack_int (int *value);

  
  size_t get_packed_short_size (size_t curr_offset);
  int pack_short (short *value);
  int unpack_short (short *value);

  size_t get_packed_bigint_size (size_t curr_offset);
  int pack_bigint (DB_BIGINT *value);
  int unpack_bigint (DB_BIGINT *value);

  int pack_int_array (const int *array, const int count);
  int unpack_int_array (int *array, int &count);

  /* TODO[arnia] : remove these if not needed */
  size_t get_packed_int_vector_size (size_t curr_offset, const int count);
  int pack_int_vector (const std::vector<int> &array);
  int unpack_int_vector (std::vector <int> &array);

  size_t get_packed_db_value_size (const DB_VALUE &value, size_t curr_offset);
  int pack_db_value (const DB_VALUE &value);
  int unpack_db_value (DB_VALUE *value);

  size_t get_packed_small_string_size (const char *string, const size_t curr_offset);
  int pack_small_string (const char *string);
  int unpack_small_string (char *string, const size_t max_size);

  size_t get_packed_large_string_size (const std::string &str, const size_t curr_offset);
  int pack_large_string (const std::string &str);
  int unpack_large_string (std::string &str);

  BUFFER_UNIT *get_curr_ptr (void) { return m_ptr; };

private:
  BUFFER_UNIT *m_ptr;       /* current pointer of serialization */
  BUFFER_UNIT *m_end_ptr;     /* end of avaialable serialization scope */
};


#endif /* _REPLICATION_SERIALIZATION_HPP_ */
