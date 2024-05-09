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

#include "method_struct_schema_info.hpp"

#include "language_support.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
  static std::vector<column_info> get_schema_table_meta ();
  static std::vector<column_info> get_schema_query_spec_meta ();
  static std::vector<column_info> get_schema_attr_meta ();
  static std::vector<column_info> get_schema_method_meta ();
  static std::vector<column_info> get_schema_methodfile_meta ();
  static std::vector<column_info> get_schema_superclasss_meta ();
  static std::vector<column_info> get_schema_constraint_meta ();
  static std::vector<column_info> get_schema_trigger_meta ();
  static std::vector<column_info> get_schema_classpriv_meta ();
  static std::vector<column_info> get_schema_attrpriv_meta ();
  static std::vector<column_info> get_schema_directsuper_meta ();
  static std::vector<column_info> get_schema_primarykey_meta ();
  static std::vector<column_info> get_schema_fk_info_meta ();

  void
  schema_info::set_schema_info (int schema_type)
  {
    switch (schema_type)
      {
      case SCH_CLASS:
      case SCH_VCLASS:
	column_infos = get_schema_table_meta ();
	break;
      case SCH_QUERY_SPEC:
	column_infos = get_schema_query_spec_meta ();
	break;
      case SCH_ATTRIBUTE:
      case SCH_CLASS_ATTRIBUTE:
	column_infos = get_schema_attr_meta ();
	break;
      case SCH_METHOD:
      case SCH_CLASS_METHOD:
	column_infos = get_schema_method_meta ();
	break;
      case SCH_METHOD_FILE:
	column_infos = get_schema_methodfile_meta ();
	break;
      case SCH_SUPERCLASS:
      case SCH_SUBCLASS:
	column_infos = get_schema_superclasss_meta ();
	break;
      case SCH_CONSTRAINT:
	column_infos = get_schema_constraint_meta ();
	break;
      case SCH_TRIGGER:
	column_infos = get_schema_trigger_meta ();
	break;
      case SCH_CLASS_PRIVILEGE:
	column_infos = get_schema_classpriv_meta ();
	break;
      case SCH_ATTR_PRIVILEGE:
	column_infos = get_schema_attrpriv_meta ();
	break;
      case SCH_DIRECT_SUPER_CLASS:
	column_infos = get_schema_directsuper_meta ();
	break;
      case SCH_PRIMARY_KEY:
	column_infos = get_schema_primarykey_meta ();
	break;
      case SCH_IMPORTED_KEYS:
      case SCH_EXPORTED_KEYS:
      case SCH_CROSS_REFERENCE:
	column_infos = get_schema_fk_info_meta ();
	break;
      default:
	/* should never happend */
	assert (false);
	break;
      }
  }

  void
  schema_info::pack (cubpacking::packer &serializator) const
  {
    // TODO: not implemented yet
    assert (false);
  }

  void
  schema_info::unpack (cubpacking::unpacker &deserializator)
  {
    // TODO: not implemented yet
    assert (false);
  }

  size_t
  schema_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = 0;
    // TODO: not implemented yet
    assert (false);
    return size;
  }

  std::vector<column_info> get_schema_table_meta ()
  {
    column_info name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "NAME");
    column_info type (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "TYPE");
    column_info remarks (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_CLASS_COMMENT_LENGTH, lang_charset(), "REMARKS");
    return std::vector<column_info> {name, type, remarks};
  }

  std::vector<column_info> get_schema_query_spec_meta ()
  {
    column_info query_spec (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "QUERY_SPEC");
    return std::vector<column_info> {query_spec};
  }

  std::vector<column_info> get_schema_attr_meta ()
  {
    column_info attr_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ATTR_NAME");
    column_info domain (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "DOMAIN");
    column_info scale (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "SCALE");
    column_info precision (DB_TYPE_INTEGER, DB_TYPE_NULL, 0, 0, lang_charset(), "PRECISION");

    column_info indexed (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "INDEXED");
    column_info non_null (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "NON_NULL");
    column_info shared (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "SHARED");
    column_info unique (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "UNIQUE");

    column_info default_ (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "DEFAULT");
    column_info attr_order (DB_TYPE_INTEGER, DB_TYPE_NULL, 0, 0, lang_charset(), "ATTR_ORDER");

    column_info class_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "CLASS_NAME");
    column_info source_class (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "SOURCE_CLASS");
    column_info is_key (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "IS_KEY");
    column_info remarks (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_CLASS_COMMENT_LENGTH, lang_charset(), "REMARKS");
    return std::vector<column_info> {attr_name, domain, scale, precision, indexed, non_null, shared, unique, default_, attr_order, class_name, source_class, is_key, remarks};
  }

  std::vector<column_info> get_schema_method_meta ()
  {
    column_info name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "NAME");
    column_info ret_domain (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "RET_DOMAIN");
    column_info arg_domain (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ARG_DOMAIN");
    return std::vector<column_info> {name, ret_domain, arg_domain};
  }

  std::vector<column_info> get_schema_methodfile_meta ()
  {
    column_info met_file (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "METHOD_FILE");
    return std::vector<column_info> {met_file};
  }

  std::vector<column_info> get_schema_superclasss_meta ()
  {
    column_info class_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "CLASS_NAME");
    column_info type (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "TYPE");
    return std::vector<column_info> {class_name, type};
  }

  std::vector<column_info> get_schema_constraint_meta ()
  {
    column_info type (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "TYPE");
    column_info name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "NAME");
    column_info attr_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ATTR_NAME");
    column_info num_pages (DB_TYPE_INTEGER, DB_TYPE_NULL, 0, 0, lang_charset(), "NUM_PAGES");
    column_info num_keys (DB_TYPE_INTEGER, DB_TYPE_NULL, 0, 0, lang_charset(), "NUM_KEYS");
    column_info primary_key (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "PRIMARY_KEY");
    column_info key_order (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "KEY_ORDER");
    column_info asc_desc (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ASC_DESC");
    return std::vector<column_info> {type, name, attr_name, num_pages, num_keys, primary_key, key_order, asc_desc};
  }

  std::vector<column_info> get_schema_trigger_meta ()
  {
    column_info name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "NAME");
    column_info status (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "STATUS");
    column_info event (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "EVENT");
    column_info target_class (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "TARGET_CLASS");
    column_info target_attr (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "TARGET_ATTR");
    column_info action_time (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ACTION_TIME");
    column_info action (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ACTION");
    column_info priority (DB_TYPE_FLOAT, DB_TYPE_NULL, 0, 0, lang_charset(), "PRIORITY");
    column_info condition_time (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(),
				"CONDITION_TIME");
    column_info condition (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "CONDITION");
    column_info remarks (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_CLASS_COMMENT_LENGTH, lang_charset(), "REMARKS");
    return std::vector<column_info> {name, status, event, target_class, target_attr, action_time, action, priority, condition_time, condition, remarks};

  }
  std::vector<column_info> get_schema_classpriv_meta ()
  {
    column_info class_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "CLASS_NAME");
    column_info privilege (DB_TYPE_STRING, DB_TYPE_NULL, 0, 10, lang_charset(), "PRIVILEGE");
    column_info grantable (DB_TYPE_STRING, DB_TYPE_NULL, 0, 5, lang_charset(), "GRANTABLE");
    return std::vector<column_info> {class_name, privilege, grantable};
  }

  std::vector<column_info> get_schema_attrpriv_meta ()
  {
    column_info attr_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ATTR_NAME");
    column_info privilege (DB_TYPE_STRING, DB_TYPE_NULL, 0, 10, lang_charset(), "PRIVILEGE");
    column_info grantable (DB_TYPE_STRING, DB_TYPE_NULL, 0, 5, lang_charset(), "GRANTABLE");
    return std::vector<column_info> {attr_name, privilege, grantable};
  }

  std::vector<column_info> get_schema_directsuper_meta ()
  {
    column_info class_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "CLASS_NAME");
    column_info super_class_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(),
				  "SUPER_CLASS_NAME");
    return std::vector<column_info> {class_name, super_class_name};
  }

  std::vector<column_info> get_schema_primarykey_meta ()
  {
    column_info class_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "CLASS_NAME");
    column_info attr_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ATTR_NAME");
    column_info num_keys (DB_TYPE_INTEGER, DB_TYPE_NULL, 0, 0, lang_charset(), "KEY_SEQ");
    column_info key_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "KEY_NAME");
    return std::vector<column_info> {class_name, attr_name, num_keys, key_name};
  }

  std::vector<column_info> get_schema_fk_info_meta ()
  {
    column_info pktable_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "PKTABLE_NAME");
    column_info pkcolumn_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "PKCOLUMN_NAME");
    column_info fktable_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "FKTABLE_NAME");
    column_info fkcolumn_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "FKCOLUMN_NAME");
    column_info key_seq (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "KEY_SEQ");
    column_info update_rule (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "UPDATE_RULE");
    column_info delete_rule (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "DELETE_RULE");
    column_info fk_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "FK_NAME");
    column_info pk_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "PK_NAME");
    return std::vector<column_info> {pktable_name, pkcolumn_name, fktable_name, fkcolumn_name, key_seq, update_rule, delete_rule, fk_name, pk_name};
  }

}
