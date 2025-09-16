/*
 *
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
 * schema_system_catalog_install.hpp
 */

#ifndef _SCHEMA_SYSTEM_CATALOG_INSTALL_HPP_
#define _SCHEMA_SYSTEM_CATALOG_INSTALL_HPP_

namespace cubschema
{
  // forward definitions
  struct system_catalog_definition;

  class system_catalog_initializer
  {
    public:
      // classes
      static system_catalog_definition get_class ();
      static system_catalog_definition get_attribute ();
      static system_catalog_definition get_domain ();
      static system_catalog_definition get_method ();
      static system_catalog_definition get_meth_sig ();
      static system_catalog_definition get_meth_arg ();
      static system_catalog_definition get_meth_file ();
      static system_catalog_definition get_query_spec ();
      static system_catalog_definition get_index ();
      static system_catalog_definition get_index_key ();
      static system_catalog_definition get_auth ();
      static system_catalog_definition get_trigger ();
      static system_catalog_definition get_partition ();
      static system_catalog_definition get_data_type ();
      static system_catalog_definition get_stored_procedure ();
      static system_catalog_definition get_stored_procedure_code ();
      static system_catalog_definition get_stored_procedure_args ();
      static system_catalog_definition get_serial ();

      static system_catalog_definition get_ha_apply_info ();
      static system_catalog_definition get_collation ();
      static system_catalog_definition get_charset ();
      static system_catalog_definition get_dual ();
      static system_catalog_definition get_server ();
      static system_catalog_definition get_synonym ();

      // views
      static system_catalog_definition get_view_class ();
      static system_catalog_definition get_view_direct_super_class ();
      static system_catalog_definition get_view_vclass ();
      static system_catalog_definition get_view_attribute ();
      static system_catalog_definition get_view_attr_setdomain_elm ();
      static system_catalog_definition get_view_method ();
      static system_catalog_definition get_view_meth_arg ();
      static system_catalog_definition get_view_meth_arg_setdomain_elm ();
      static system_catalog_definition get_view_meth_file ();
      static system_catalog_definition get_view_index ();
      static system_catalog_definition get_view_index_key ();
      static system_catalog_definition get_view_auth ();
      static system_catalog_definition get_view_trigger ();
      static system_catalog_definition get_view_partition ();
      static system_catalog_definition get_view_stored_procedure ();
      static system_catalog_definition get_view_stored_procedure_args ();
      static system_catalog_definition get_view_serial ();
      static system_catalog_definition get_view_ha_apply_info ();
      static system_catalog_definition get_view_collation ();
      static system_catalog_definition get_view_charset ();
      static system_catalog_definition get_view_synonym ();
      static system_catalog_definition get_view_server ();
  };
}

// TODO: move them to proper place
const char *sm_define_view_class_spec (void);
const char *sm_define_view_direct_super_class_spec (void);
const char *sm_define_view_vclass_spec (void);
const char *sm_define_view_attribute_spec (void);
const char *sm_define_view_attr_setdomain_elm_spec (void);
const char *sm_define_view_method_spec (void);
const char *sm_define_view_method_arg_spec (void);
const char *sm_define_view_meth_arg_setdomain_elm_spec (void);
const char *sm_define_view_meth_file_spec (void);
const char *sm_define_view_index_spec (void);
const char *sm_define_view_index_key_spec (void);
const char *sm_define_view_auth_spec (void);
const char *sm_define_view_trigger_spec (void);
const char *sm_define_view_partition_spec (void);
const char *sm_define_view_stored_procedure_spec (void);
const char *sm_define_view_stored_procedure_args_spec (void);
const char *sm_define_view_serial_spec (void);
const char *sm_define_view_ha_apply_info_spec (void);
const char *sm_define_view_collation_spec (void);
const char *sm_define_view_charset_spec (void);
const char *sm_define_view_synonym_spec (void);
const char *sm_define_view_server_spec (void);

#endif /* _SCHEMA_SYSTEM_CATALOG_INSTALL_HPP_ */
