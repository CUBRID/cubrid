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
#include "load_error_handler.hpp"

namespace cubload
{

  // forward declaration
  class session;

  class server_base_loader
  {
    public:
      server_base_loader () = delete;
      server_base_loader (session &session, error_handler &error_handler);
      virtual ~server_base_loader () = default;

    protected:
      session &m_session;
      error_handler &m_error_handler;
      cubthread::entry *m_thread_ref;

      class_id m_clsid;

      bool m_attrinfo_started;
      heap_cache_attrinfo m_attrinfo;

      bool m_scancache_started;
      heap_scancache m_scancache;

      void start_scancache (const OID &class_oid);
      void stop_scancache ();

      void start_attrinfo (const OID &class_oid);
      void stop_attrinfo ();
  };

  class server_class_installer : protected server_base_loader, public class_installer
  {
    public:
      server_class_installer () = delete;
      server_class_installer (session &session, error_handler &error_handler);
      ~server_class_installer () override = default;

      void init (class_id clsid) override;

      void check_class (const char *class_name, int class_id) override;
      int install_class (const char *class_name) override;
      void install_class (string_type *class_name, class_command_spec_type *cmd_spec) override;

    private:
      void locate_class (const char *class_name, OID &class_oid);

      void register_class (const char *class_name, string_type *attr_list);
      void register_class_attributes (class_entry *cls_entry, string_type *attr_list);
  };

  class server_object_loader : protected server_base_loader, public object_loader
  {
    public:
      server_object_loader () = delete;
      server_object_loader (session &session, error_handler &error_handler);
      ~server_object_loader () override = default;

      void init (class_id clsid) override;
      void destroy () override;

      void start_line (int object_id) override;
      void process_line (constant_type *cons) override;
      void finish_line () override;

    private:
      void process_constant (constant_type *cons, attribute &attr);
      void process_monetary_constant (constant_type *cons, tp_domain *domain, db_value *db_val);

      class_entry *m_class_entry;
  };

} // namespace cubload
#endif /* _LOAD_SERVER_LOADER_HPP_ */
