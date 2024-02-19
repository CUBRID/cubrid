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
 * schema_system_catalog_constants.h - declare system catalog name constants
 */

#ifndef _SCHEMA_SYSTEM_CATALOG_CONSTANTS_H_
#define _SCHEMA_SYSTEM_CATALOG_CONSTANTS_H_

/* catalog classes */
#define CT_CLASS_NAME              "_db_class"
#define CT_ATTRIBUTE_NAME          "_db_attribute"
#define CT_DOMAIN_NAME             "_db_domain"
#define CT_METHOD_NAME             "_db_method"
#define CT_METHSIG_NAME            "_db_meth_sig"
#define CT_METHARG_NAME            "_db_meth_arg"
#define CT_METHFILE_NAME           "_db_meth_file"
#define CT_QUERYSPEC_NAME          "_db_query_spec"
#define CT_RESOLUTION_NAME         "_db_resolution"
#define CT_INDEX_NAME              "_db_index"
#define CT_INDEXKEY_NAME           "_db_index_key"
#define CT_CLASSAUTH_NAME          "_db_auth"
#define CT_DATATYPE_NAME           "_db_data_type"
#define CT_STORED_PROC_NAME        "_db_stored_procedure"
#define CT_STORED_PROC_ARGS_NAME   "_db_stored_procedure_args"
#define CT_PARTITION_NAME          "_db_partition"
#define CT_SERIAL_NAME             "db_serial"
#define CT_HA_APPLY_INFO_NAME      "db_ha_apply_info"
#define CT_COLLATION_NAME          "_db_collation"
#define CT_USER_NAME               "db_user"
#define CT_TRIGGER_NAME            "db_trigger"
#define CT_ROOT_NAME               "db_root"
#define CT_PASSWORD_NAME           "db_password"
#define CT_AUTHORIZATION_NAME      "db_authorization"
#define CT_AUTHORIZATIONS_NAME     "db_authorizations"
#define CT_CHARSET_NAME		   "_db_charset"
#define CT_DUAL_NAME               "dual"
#define CT_DB_SERVER_NAME          "_db_server"
#define CT_SYNONYM_NAME            "_db_synonym"

/* catalog vclasses */
#define CTV_CLASS_NAME             "db_class"
#define CTV_SUPER_CLASS_NAME       "db_direct_super_class"
#define CTV_VCLASS_NAME            "db_vclass"
#define CTV_ATTRIBUTE_NAME         "db_attribute"
#define CTV_ATTR_SD_NAME           "db_attr_setdomain_elm"
#define CTV_METHOD_NAME            "db_method"
#define CTV_METHARG_NAME           "db_meth_arg"
#define CTV_METHARG_SD_NAME        "db_meth_arg_setdomain_elm"
#define CTV_METHFILE_NAME          "db_meth_file"
#define CTV_INDEX_NAME             "db_index"
#define CTV_INDEXKEY_NAME          "db_index_key"
#define CTV_AUTH_NAME              "db_auth"
#define CTV_TRIGGER_NAME           "db_trig"
#define CTV_STORED_PROC_NAME       "db_stored_procedure"
#define CTV_STORED_PROC_ARGS_NAME  "db_stored_procedure_args"
#define CTV_PARTITION_NAME         "db_partition"
#define CTV_DB_COLLATION_NAME      "db_collation"
#define CTV_DB_CHARSET_NAME	   "db_charset"
#define CTV_DB_SERVER_NAME         "db_server"
#define CTV_SYNONYM_NAME           "db_synonym"

#define CT_DBCOLL_COLL_ID_COLUMN	   "coll_id"
#define CT_DBCOLL_COLL_NAME_COLUMN	   "coll_name"
#define CT_DBCOLL_CHARSET_ID_COLUMN	   "charset_id"
#define CT_DBCOLL_BUILT_IN_COLUMN	   "built_in"
#define CT_DBCOLL_EXPANSIONS_COLUMN	   "expansions"
#define CT_DBCOLL_CONTRACTIONS_COLUMN	   "contractions"
#define CT_DBCOLL_UCA_STRENGTH		   "uca_strength"
#define CT_DBCOLL_CHECKSUM_COLUMN	   "checksum"

#define CT_DBCHARSET_CHARSET_ID		  "charset_id"
#define CT_DBCHARSET_CHARSET_NAME	  "charset_name"
#define CT_DBCHARSET_DEFAULT_COLLATION	  "default_collation"
#define CT_DBCHARSET_CHAR_SIZE		  "char_size"

#endif /* _SCHEMA_SYSTEM_CATALOG_CONSTANTS_H_ */
