/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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
#include "memory_private_allocator.hpp"

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

      bool is_class_ignored (const char *classname);
      void to_lowercase_identifier (const char *idname, cubmem::extensible_block &eb);

    private:
      session &m_session;
      error_handler &m_error_handler;

      class_id m_clsid;

      LC_FIND_CLASSNAME locate_class (const char *class_name, OID &class_oid);
      LC_FIND_CLASSNAME locate_class_for_all_users (const char *class_name, OID &class_oid);
      void register_class_with_attributes (const char *class_name, class_command_spec_type *cmd_spec);
      void get_class_attributes (heap_cache_attrinfo &attrinfo, attribute_type attr_type, or_attribute *&or_attributes,
				 int *n_attributes);
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
      void flush_records () override;

      std::size_t get_rows_number () override;

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
      std::vector<record_descriptor> m_recdes_collected;

      bool m_scancache_started;
      heap_scancache m_scancache;

      std::size_t m_rows;
  };

} // namespace cubload
#endif /* _LOAD_SERVER_LOADER_HPP_ */
