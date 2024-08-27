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

#include "schema_system_catalog.hpp"

#include "db.h"
#include "dbtype_function.h"
#include "identifier_store.hpp"
#include "oid.h"
#include "schema_system_catalog_constants.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

using namespace cubbase;

namespace cubschema
{
  static const std::vector <std::string> sm_system_class_names =
  {
    ROOTCLASS_NAME,			// "Rootclass"
    CT_DUAL_NAME,			// "dual"

    /*
     * authorization classes
     *
     * AU_ROOT_CLASS_NAME     = CT_ROOT_NAME
     * AU_OLD_ROOT_CLASS_NAME = CT_AUTHORIZATIONS_NAME
     * AU_USER_CLASS_NAME     = CT_USER_NAME
     * AU_PASSWORD_CLASS_NAME = CT_PASSWORD_NAME
     * AU_AUTH_CLASS_NAME     = CT_AUTHORIZATION_NAME
     * AU_GRANT_CLASS_NAME
     */
    CT_ROOT_NAME,		// "db_root"
    CT_USER_NAME,		// "db_user"
    CT_PASSWORD_NAME,	// "db_password"
    CT_AUTHORIZATION_NAME,		// "db_authorization"
    CT_AUTHORIZATIONS_NAME,	// "db_authorizations"

    /* currently, not implemented */
    // AU_GRANT_CLASS_NAME,		// "db_grant"

    /*
     * catalog classes
     */
    CT_CLASS_NAME,			// "_db_class"
    CT_ATTRIBUTE_NAME, 		// "_db_attribute"
    CT_DOMAIN_NAME,			// "_db_domain"
    CT_METHOD_NAME,			// "_db_method"
    CT_METHSIG_NAME,			// "_db_meth_sig"
    CT_METHARG_NAME,			// "_db_meth_arg"
    CT_METHFILE_NAME,		// "_db_meth_file"
    CT_QUERYSPEC_NAME,		// "_db_query_spec"
    CT_INDEX_NAME,			// "_db_index"
    CT_INDEXKEY_NAME,		// "_db_index_key"
    CT_DATATYPE_NAME,		// "_db_data_type"
    CT_CLASSAUTH_NAME,		// "_db_auth"
    CT_PARTITION_NAME,		// "_db_partition"
    CT_STORED_PROC_NAME,		// "_db_stored_procedure"
    CT_STORED_PROC_ARGS_NAME,	// "_db_stored_procedure_args"
    CT_SERIAL_NAME,			// "db_serial"
    CT_HA_APPLY_INFO_NAME,	// "db_ha_apply_info"
    CT_COLLATION_NAME,		// "_db_collation"
    CT_CHARSET_NAME,			// "_db_charset"
    CT_DB_SERVER_NAME,		// "_db_server"
    CT_SYNONYM_NAME,			// "_db_synonym"

    CT_TRIGGER_NAME,			// "db_trigger"

    /* currently, not implemented */
    CT_RESOLUTION_NAME		// "_db_resolution"
  };

  static const std::vector <std::string> sm_system_vclass_names =
  {
    /*
     * catalog vclasses
     */
    CTV_CLASS_NAME,			// "db_class"
    CTV_SUPER_CLASS_NAME,	// "db_direct_super_class"
    CTV_VCLASS_NAME,			// "db_vclass"
    CTV_ATTRIBUTE_NAME,		// "db_attribute"
    CTV_ATTR_SD_NAME,		// "db_attr_setdomain_elm"
    CTV_METHOD_NAME,			// "db_method"
    CTV_METHARG_NAME,		// "db_meth_arg"
    CTV_METHARG_SD_NAME,		// "db_meth_arg_setdomain_elm"
    CTV_METHFILE_NAME,		// "db_meth_file"
    CTV_INDEX_NAME,			// "db_index"
    CTV_INDEXKEY_NAME,		// "db_index_key"
    CTV_AUTH_NAME,			// "db_auth"
    CTV_TRIGGER_NAME,		// "db_trig"
    CTV_PARTITION_NAME,		// "db_partition"
    CTV_STORED_PROC_NAME,	// "db_stored_procedure"
    CTV_STORED_PROC_ARGS_NAME,	// "db_stored_procedure_args"
    CTV_DB_COLLATION_NAME,	// "db_collation"
    CTV_DB_CHARSET_NAME,		// "db_charset"
    CTV_DB_SERVER_NAME,		// "db_server"
    CTV_SYNONYM_NAME			// "db_synonym"
  };

  static const identifier_store sm_catalog_class_names (sm_system_class_names, false);
  static const identifier_store sm_catalog_vclass_names (sm_system_vclass_names, false);
}

bool sm_check_system_class_by_name (const std::string_view name)
{
  // TODO: bool is_enclosed = identifier_store::is_enclosed (name);
  return identifier_store::check_identifier_is_valid (name, false)
	 && (sm_is_system_class (name) || sm_is_system_vclass (name));
}

bool sm_is_system_class (const std::string_view name)
{
  return cubschema::sm_catalog_class_names.is_exists (name);
}

bool sm_is_system_vclass (const std::string_view name)
{
  return cubschema::sm_catalog_vclass_names.is_exists (name);
}
