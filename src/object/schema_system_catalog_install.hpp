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
  struct catcls_function;

  class system_catalog_initializer
  {
    public:
      // classes
      static system_catalog_definition get_class ();
      static system_catalog_definition get_attribute ();
      static system_catalog_definition get_domain ();
      static system_catalog_definition get_method ();
      static system_catalog_definition get_method_sig ();
      static system_catalog_definition get_meth_argument ();
      static system_catalog_definition get_meth_file ();
      static system_catalog_definition get_query_spec ();
      static system_catalog_definition get_index ();
      static system_catalog_definition get_index_key ();
      static system_catalog_definition get_class_authorization ();
      static system_catalog_definition get_partition ();
      static system_catalog_definition get_data_type ();
      static system_catalog_definition get_stored_procedure ();
      static system_catalog_definition get_stored_procedure_arguments ();
      static system_catalog_definition get_serial ();

      static system_catalog_definition get_ha_apply_info ();
      static system_catalog_definition get_collations ();
      static system_catalog_definition get_charsets ();
      static system_catalog_definition get_dual ();
      static system_catalog_definition get_db_server ();
      static system_catalog_definition get_synonym ();

      // views
      static system_catalog_definition get_view_class ();
      static system_catalog_definition get_view_super_class ();
      static system_catalog_definition get_view_vclass ();
      static system_catalog_definition get_view_attribute ();
      static system_catalog_definition get_view_attribute_set_domain ();
      static system_catalog_definition get_view_method ();
      static system_catalog_definition get_view_method_argument ();
      static system_catalog_definition get_view_method_argument_set_domain ();
      static system_catalog_definition get_view_method_file ();
      static system_catalog_definition get_view_index ();
      static system_catalog_definition get_view_index_key ();
      static system_catalog_definition get_view_authorization ();
      static system_catalog_definition get_view_trigger ();
      static system_catalog_definition get_view_partition ();
      static system_catalog_definition get_view_stored_procedure ();
      static system_catalog_definition get_view_stored_procedure_arguments ();
      static system_catalog_definition get_view_db_collation ();
      static system_catalog_definition get_view_db_charset ();
      static system_catalog_definition get_view_synonym ();
      static system_catalog_definition get_view_db_server ();
  };
}

// TODO: move them to proper place
const char *sm_define_view_class_spec (void);
const char *sm_define_view_super_class_spec (void);
const char *sm_define_view_vclass_spec (void);
const char *sm_define_view_attribute_spec (void);
const char *sm_define_view_attribute_set_domain_spec (void);
const char *sm_define_view_method_spec (void);
const char *sm_define_view_method_argument_spec (void);
const char *sm_define_view_method_argument_set_domain_spec (void);
const char *sm_define_view_method_file_spec (void);
const char *sm_define_view_index_spec (void);
const char *sm_define_view_index_key_spec (void);
const char *sm_define_view_authorization_spec (void);
const char *sm_define_view_trigger_spec (void);
const char *sm_define_view_partition_spec (void);
const char *sm_define_view_stored_procedure_spec (void);
const char *sm_define_view_stored_procedure_arguments_spec (void);
const char *sm_define_view_db_collation_spec (void);
const char *sm_define_view_db_charset_spec (void);
const char *sm_define_view_synonym_spec (void);
const char *sm_define_view_db_server_spec (void);

#endif /* _SCHEMA_SYSTEM_CATALOG_INSTALL_HPP_ */
