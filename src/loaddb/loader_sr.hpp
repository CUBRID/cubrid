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
 * loader_sr.hpp: Loader server definitions. Updated using design from fast loaddb prototype
 */

#ifndef _LOADER_SR_HPP_
#define _LOADER_SR_HPP_

#ident "$Id$"

#include "common.hpp"
#include "dbtype_def.h"
#include "heap_file.h"

namespace cubload
{

  using oid_t = OID;
  using attr_id_t = ATTR_ID;
  using scan_cache_t = HEAP_SCANCACHE;
  using attr_info_t = HEAP_CACHE_ATTRINFO;

  class server_loader : public loader
  {
    public:
      server_loader ();
      ~server_loader () override;

      void act_setup_class_command_spec (string_t **class_name, class_cmd_spec_t **cmd_spec) override;
      void act_start_id (char *name) override;
      void act_set_id (int id) override;
      void act_start_instance (int id, constant_t *cons) override;
      void process_constants (constant_t *cons) override;
      void act_finish_line () override;
      void act_finish () override;

      void load_failed_error () override;
      void increment_err_total () override;
      void increment_fails () override;

    private:
      oid_t m_class_oid;
      attr_id_t *m_attr_id;

      attr_info_t m_attr_info;
  };

} // namespace cubload
#endif /* _LOADER_SR_HPP_ */
