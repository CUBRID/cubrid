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

#include <vector>

namespace cubload
{

  // forward declaration
  class session;
  class attribute;
  class class_entry;
  class error_handler;

  class server_class_installer : public class_installer
  {
    public:
      server_class_installer () = delete;
      server_class_installer (session &session, error_handler &error_handler);
      ~server_class_installer () override = default;

      void set_class_id (class_id clsid) override;

      void check_class (const char *class_name, int class_id) override;
      int install_class (const char *class_name) override;
      void install_class (string_type *class_name, class_command_spec_type *cmd_spec) override;

    private:
      session &m_session;
      error_handler &m_error_handler;

      class_id m_clsid;

      void locate_class (const char *class_name, OID &class_oid);
      void register_class_with_attributes (const char *class_name, class_command_spec_type *cmd_spec);
      void get_class_attributes (heap_cache_attrinfo &attrinfo, attribute_type attr_type, REFPTR (or_attribute,
				 or_attributes), int *n_attributes);
  };

  class server_object_loader : public object_loader
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
      int process_constant (constant_type *cons, const attribute &attr);
      int process_generic_constant (constant_type *cons, const attribute &attr);
      int process_monetary_constant (constant_type *cons, const attribute &attr);
      int process_collection_constant (constant_type *cons, const attribute &attr);

      void clear_db_values ();
      db_value &get_attribute_db_value (size_t attr_index);

      void start_scancache (const OID &class_oid);
      void stop_scancache ();

      void start_attrinfo (const OID &class_oid);
      void stop_attrinfo ();

      session &m_session;
      error_handler &m_error_handler;
      cubthread::entry *m_thread_ref;

      class_id m_clsid;

      const class_entry *m_class_entry;
      bool m_attrinfo_started;
      heap_cache_attrinfo m_attrinfo;
      std::vector<db_value> m_db_values;

      bool m_scancache_started;
      heap_scancache m_scancache;
  };

} // namespace cubload
#endif /* _LOAD_SERVER_LOADER_HPP_ */
