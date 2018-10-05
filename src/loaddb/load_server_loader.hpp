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
 * load_server_loader.hpp: Loader server definitions. Updated using design from fast loaddb prototype
 */

#ifndef _LOAD_SERVER_LOADER_HPP_
#define _LOAD_SERVER_LOADER_HPP_

#include "dbtype_def.h"
#include "heap_attrinfo.h"
#include "heap_file.h"
#include "load_common.hpp"

namespace cubload
{

  // forward declaration
  class session;
  class driver;

  class server_loader : public loader
  {
    public:
      server_loader (session &session, driver &driver);
      ~server_loader () override;

      void check_class (const char *class_name, int class_id) override;
      int setup_class (const char *class_name) override;
      void setup_class (string_type *class_name, class_command_spec_type *cmd_spec) override;
      void destroy () override;

      void start_line (int object_id) override;
      void process_line (constant_type *cons) override;
      void finish_line () override;

    private:
      void process_constant (constant_type *cons, int attr_idx);
      void process_monetary_constant (constant_type *cons, tp_domain *domain, db_value *db_val);

      session &m_session;
      driver &m_driver;

      OID m_class_oid;

      ATTR_ID *m_attr_ids;
      heap_cache_attrinfo m_attr_info;

      heap_scancache m_scancache;
      bool m_scancache_started;
  };
} // namespace cubload
#endif /* _LOAD_SERVER_LOADER_HPP_ */
