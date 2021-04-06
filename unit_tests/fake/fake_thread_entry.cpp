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

#include "thread_entry.hpp"

namespace cubbase
{
  template <typename Res>
  class resource_tracker
  {
  };
  alloc_tracker g_at;
  pgbuf_tracker g_pt;
}
namespace cuberr
{
  bool g_logging = false;
  er_message::er_message (const bool &) : m_logging { g_logging } {};
  er_message::~er_message () {};
  context::context (bool, bool) : m_base_level (g_logging) {};
  context::~context () {};
}
namespace cubsync
{
  class critical_section_tracker
  {
  };
  critical_section_tracker g_cst;
}
namespace cubthread
{
  // todo: move this to common test utils
  entry::entry ()
    : m_alloc_tracker { cubbase::g_at }
    , m_pgbuf_tracker { cubbase::g_pt }
    , m_csect_tracker { cubsync::g_cst }
  {
  }
  entry::~entry ()
  {
  }

  entry g_entry;
  entry &get_entry ()
  {
    return g_entry;
  }
}
