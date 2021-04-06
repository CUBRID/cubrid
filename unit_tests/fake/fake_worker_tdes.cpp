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

// fake all objects that worker tdes depends on
// replication is "disabled"

#include "log_impl.h"

bool log_tdes::is_active_worker_transaction () const
{
  return true;
}

bool log_tdes::is_system_transaction () const
{
  return false;
}

bool log_tdes::is_allowed_sysop () const
{
  return true;
}

void log_tdes::lock_topop ()
{
}

void log_tdes::unlock_topop ()
{
}

void log_tdes::on_sysop_start ()
{
}

void log_tdes::on_sysop_end ()
{
}

cubreplication::log_generator &
log_tdes::get_replication_generator ()
{
  return replication_log_generator;
}

// external dependencies:
//
//    mem_block.cpp for cubmem::STANDARD_BLOCK_ALLOCATOR
//    packer.cpp for packer/unpacker
//    client_credentials.cpp for clientids ...
//      ... porting.c for basename_r
//

// fix dependencies locally:

mvcc_active_tran::~mvcc_active_tran () = default;

mvcc_info::mvcc_info () = default;

int or_packed_value_size (const DB_VALUE *value, int collapse_null, int include_domain, int include_domain_classoids)
{
  return 0;
}

char *
or_pack_value (char *buf, DB_VALUE *value)
{
  return nullptr;
}

char *
or_unpack_value (const char *buf, DB_VALUE *value)
{
  return nullptr;
}

namespace cubreplication
{
  // stream_entry
  size_t
  stream_entry::get_packed_header_size ()
  {
    return 0;
  }

  size_t
  stream_entry::get_data_packed_size ()
  {
    return 0;
  }

  void
  stream_entry::set_header_data_size (const size_t &data_size)
  {
  }

  cubbase::factory<int, replication_object> *
  stream_entry::get_builder ()
  {
    return nullptr;
  }

  int stream_entry::pack_stream_entry_header ()
  {
    return 0;
  }

  int stream_entry::unpack_stream_entry_header ()
  {
    return 0;
  }

  int stream_entry::get_packable_entry_count_from_header ()
  {
    return 0;
  }

  bool
  stream_entry::is_equal (const cubstream::entry<replication_object> *other)
  {
    return true;
  }

  // log_generator
  log_generator::~log_generator ()
  {
  }
} // namespace cubreplication
