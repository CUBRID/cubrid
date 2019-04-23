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
 * replication_db_copy.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_DB_COPY_HPP_
#define _REPLICATION_DB_COPY_HPP_

#include "heap_attrinfo.h"  /* for HEAP_CACHE_ATTRINFO */

class record_descriptor;

namespace cubstream
{
  class multi_thread_stream;
};

namespace cubpacking
{
  class packer;
  class unpacker;
};

namespace cubreplication
{
  class replication_object;
  class stream_entry;
  class row_object;

  class copy_context
    {
    public:
      copy_context ()
        {
          m_stream = NULL;
        }

      ~copy_context () {}

      void pack_and_add_object (row_object &obj);
    private:
      cubstream::multi_thread_stream *m_stream;
    };

  /* packs multiple records (recdes) from heap to be copied into a new replicated server */
  class row_object : public replication_object
    {
    public:
      static const size_t DATA_PACK_THRESHOLD_SIZE = 16384;
      static const size_t DATA_PACK_THRESHOLD_CNT = 100;

      static const int PACKING_ID = 5;

      row_object (const char *class_name);

      ~row_object ();

      void reset ();

      int apply () override;

      void pack (cubpacking::packer &serializator) const final;
      void unpack (cubpacking::unpacker &deserializator) final;
      std::size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const final;

      bool is_equal (const cubpacking::packable_object *other) final;
      void stringify (string_buffer &str) final;

      void add_record (const record_descriptor &record);

      int convert_to_last_representation (cubthread::entry *thread_p, record_descriptor &record,
                                          const OID &inst_oid, HEAP_CACHE_ATTRINFO &attr_info);

      bool is_pack_needed (void)
        {
          return m_rec_des_list.size () >= DATA_PACK_THRESHOLD_CNT || m_data_size >= DATA_PACK_THRESHOLD_SIZE;
        }

    private:
      std::vector<record_descriptor> m_rec_des_list;
      std::string m_class_name;

      /* non-serialized data members: */
      /* total size of all record_descriptor buffers */
      std::size_t m_data_size;

      LC_COPYAREA *m_copyarea;
    };

} /* namespace cubreplication */

#endif /* _REPLICATION_DB_COPY_HPP_ */
