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

#include <string>

namespace cubschema
{
  // forward definitions
  struct system_metadata_definition;

  namespace system_catalog_initializer
  {
    // classes
    system_metadata_definition get_class ();
    system_metadata_definition get_attribute ();
    system_metadata_definition get_domain ();
    system_metadata_definition get_method ();
    system_metadata_definition get_meth_sig ();
    system_metadata_definition get_meth_arg ();
    system_metadata_definition get_meth_file ();
    system_metadata_definition get_query_spec ();
    system_metadata_definition get_index ();
    system_metadata_definition get_index_key ();
    system_metadata_definition get_auth ();
    system_metadata_definition get_trigger ();
    system_metadata_definition get_partition ();
    system_metadata_definition get_data_type ();
    system_metadata_definition get_stored_procedure ();
    system_metadata_definition get_stored_procedure_code ();
    system_metadata_definition get_stored_procedure_args ();
    system_metadata_definition get_serial ();

    system_metadata_definition get_ha_apply_info ();
    system_metadata_definition get_collation ();
    system_metadata_definition get_charset ();
    system_metadata_definition get_dual ();
    system_metadata_definition get_server ();
    system_metadata_definition get_synonym ();
    system_metadata_definition get_histogram ();
    system_metadata_definition get_global_tran ();

    // views
    system_metadata_definition get_view_class ();
    system_metadata_definition get_view_direct_super_class ();
    system_metadata_definition get_view_vclass ();
    system_metadata_definition get_view_attribute ();
    system_metadata_definition get_view_attr_setdomain_elm ();
    system_metadata_definition get_view_method ();
    system_metadata_definition get_view_meth_arg ();
    system_metadata_definition get_view_meth_arg_setdomain_elm ();
    system_metadata_definition get_view_meth_file ();
    system_metadata_definition get_view_index ();
    system_metadata_definition get_view_index_key ();
    system_metadata_definition get_view_auth ();
    system_metadata_definition get_view_trigger ();
    system_metadata_definition get_view_partition ();
    system_metadata_definition get_view_stored_procedure ();
    system_metadata_definition get_view_stored_procedure_args ();
    system_metadata_definition get_view_serial ();
    system_metadata_definition get_view_ha_apply_info ();
    system_metadata_definition get_view_collation ();
    system_metadata_definition get_view_user ();
    system_metadata_definition get_view_authorization ();
    system_metadata_definition get_view_charset ();
    system_metadata_definition get_view_synonym ();
    system_metadata_definition get_view_server ();
    system_metadata_definition get_view_db_histogram ();
  }
} // namespace cubschema

std::string sm_define_view_class_spec (void);
std::string sm_define_view_direct_super_class_spec (void);
std::string sm_define_view_vclass_spec (void);
std::string sm_define_view_attribute_spec (void);
std::string sm_define_view_attr_setdomain_elm_spec (void);
std::string sm_define_view_method_spec (void);
std::string sm_define_view_method_arg_spec (void);
std::string sm_define_view_meth_arg_setdomain_elm_spec (void);
std::string sm_define_view_meth_file_spec (void);
std::string sm_define_view_index_spec (void);
std::string sm_define_view_index_key_spec (void);
std::string sm_define_view_auth_spec (void);
std::string sm_define_view_trigger_spec (void);
std::string sm_define_view_partition_spec (void);
std::string sm_define_view_stored_procedure_spec (void);
std::string sm_define_view_stored_procedure_args_spec (void);
std::string sm_define_view_serial_spec (void);
std::string sm_define_view_ha_apply_info_spec (void);
std::string sm_define_view_collation_spec (void);
std::string sm_define_view_user_spec (void);
std::string sm_define_view_authorization_spec (void);
std::string sm_define_view_charset_spec (void);
std::string sm_define_view_synonym_spec (void);
std::string sm_define_view_server_spec (void);
std::string sm_define_view_histogram_spec (void);

#endif /* _SCHEMA_SYSTEM_CATALOG_INSTALL_HPP_ */
