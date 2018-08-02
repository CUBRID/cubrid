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

#ident "$Id$"

#include "dbtype_def.h"
#include "heap_attrinfo.h"
#include "heap_file.h"
#include "load_common.hpp"
#include "storage_common.h"

namespace cubload
{

  class server_loader : public loader
  {
    public:
      server_loader ();
      ~server_loader () override;

      void act_setup_class_command_spec (string_type **class_name, class_command_spec_type **cmd_spec) override;
      void act_start_id (char *name) override;
      void act_set_id (int id) override;
      void act_start_instance (int id, constant_type *cons) override;
      void process_constants (constant_type *cons) override;
      void act_finish_line () override;
      void act_finish () override;

      void load_failed_error () override;
      void increment_err_total () override;
      void increment_fails () override;

    private:
      OID m_class_oid;
      ATTR_ID *m_attr_ids;
      HEAP_CACHE_ATTRINFO m_attr_info;

      int m_err_total;
      int m_total_fails;
  };

} // namespace cubload
#endif /* _LOAD_SERVER_LOADER_HPP_ */
