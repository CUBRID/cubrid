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
 * schema_system_catalog_install.cpp
 */

#include "schema_system_catalog_install.hpp"

#include "schema_system_catalog.hpp"
#include "db.h"
#include "dbtype_function.h"
#include "schema_system_catalog_constants.h"
#include "sp_constants.hpp"
#include "trigger_manager.h"
#include "work_space.h"
#include "schema_system_catalog_builder.hpp"
#include "schema_system_catalog_definition.hpp"
#include "authenticate.h"

#define CT_DUAL_DUMMY   "dummy"

using namespace std::literals;

static std::function<int (DB_VALUE *)>
make_int_value_fn (int num)
{
  return [num] (DB_VALUE* val)
  {
    return db_make_int (val, num);
  };
}

static std::function<int (DB_VALUE *)>
make_double_value_fn (double num)
{
  return [num] (DB_VALUE* val)
  {
    return db_make_double (val, num);
  };
}

static std::function<int (DB_VALUE *)>
make_numeric_value_fn (const char *str)
{
  return [str] (DB_VALUE *val)
  {
    int error_code = NO_ERROR;
    error_code = numeric_coerce_string_to_num (str, strlen (str), LANG_SYS_CODESET, val);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    FLOAT_TO_FIXED_NUMERIC (val);
    return error_code;
  };
}

/* ========================================================================== */
/* NEW DEFINITION (initializers for CLASS) */
/* ========================================================================== */
int
catcls_add_data_type (DB_OBJECT *class_mop)
{
  DB_OBJECT *obj;
  DB_VALUE val;
  int i;

  const char *names[DB_TYPE_LAST] =
  {
    "INTEGER", "FLOAT", "DOUBLE", "STRING", "OBJECT",
    "SET", "MULTISET", "SEQUENCE", "ELO", "TIME",
    "TIMESTAMP", "DATE", "MONETARY", NULL /* VARIABLE */, NULL /* SUB */,
    NULL /* POINTER */, NULL /* ERROR */, "SHORT", NULL /* VOBJ */,
    NULL /* OID */,
    NULL /* VALUE */, "NUMERIC", "BIT", "VARBIT", "CHAR",

    /* TODO:
     * DB_TYPE_NCHAR and DB_TYPE_VARNCHAR will no longer be used(NCHAR was deprecated).
     * However, to maintain compatibility with previous versions, the enum list will be preserved.
     */
    NULL /* NCHAR */, NULL /* VARNCHAR */,

    NULL /* RESULTSET */, NULL /* MIDXKEY */,
    NULL /* TABLE */,
    "BIGINT", "DATETIME",
    "BLOB", "CLOB", "ENUM",
    "TIMESTAMPTZ", "TIMESTAMPLTZ", "DATETIMETZ", "DATETIMELTZ",
    "JSON"
  };

  for (i = 0; i < DB_TYPE_LAST; i++)
    {
      if (names[i] != NULL)
	{
	  obj = db_create_internal (class_mop);
	  if (obj == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }

	  db_make_int (&val, i + 1);
	  db_put_internal (obj, "type_id", &val);

	  db_make_varchar (&val, 16, names[i], strlen (names[i]), LANG_SYS_CODESET, LANG_SYS_COLLATION);
	  db_put_internal (obj, "type_name", &val);
	}
    }

  return NO_ERROR;
}

int
catcls_add_collations (DB_OBJECT *class_mop)
{
  int i;
  int count_collations;
  int found_coll = 0;

  count_collations = lang_collation_count ();

  for (i = 0; i < LANG_MAX_COLLATIONS; i++)
    {
      LANG_COLLATION *lang_coll = lang_get_collation (i);
      DB_OBJECT *obj;
      DB_VALUE val;

      assert (lang_coll != NULL);

      if (i != 0 && lang_coll->coll.coll_id == LANG_COLL_DEFAULT)
	{
	  /* iso88591 binary collation added only once */
	  continue;
	}
      found_coll++;

      obj = db_create_internal (class_mop);
      if (obj == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      assert (lang_coll->coll.coll_id == i);

      db_make_int (&val, i);
      db_put_internal (obj, CT_DBCOLL_COLL_ID_COLUMN, &val);

      db_make_varchar (&val, 32, lang_coll->coll.coll_name, strlen (lang_coll->coll.coll_name), LANG_SYS_CODESET,
		       LANG_SYS_COLLATION);
      db_put_internal (obj, CT_DBCOLL_COLL_NAME_COLUMN, &val);

      db_make_int (&val, (int) (lang_coll->codeset));
      db_put_internal (obj, CT_DBCOLL_CHARSET_ID_COLUMN, &val);

      db_make_int (&val, lang_coll->built_in);
      db_put_internal (obj, CT_DBCOLL_BUILT_IN_COLUMN, &val);

      db_make_int (&val, lang_coll->coll.uca_opt.sett_expansions ? 1 : 0);
      db_put_internal (obj, CT_DBCOLL_EXPANSIONS_COLUMN, &val);

      db_make_int (&val, lang_coll->coll.count_contr);
      db_put_internal (obj, CT_DBCOLL_CONTRACTIONS_COLUMN, &val);

      db_make_int (&val, (int) (lang_coll->coll.uca_opt.sett_strength));
      db_put_internal (obj, CT_DBCOLL_UCA_STRENGTH, &val);

      assert (strlen (lang_coll->coll.checksum) == 32);
      db_make_varchar (&val, 32, lang_coll->coll.checksum, 32, LANG_SYS_CODESET, LANG_SYS_COLLATION);
      db_put_internal (obj, CT_DBCOLL_CHECKSUM_COLUMN, &val);
    }

  assert (found_coll == count_collations);

  return NO_ERROR;
}

static int
catcls_add_dual (DB_OBJECT *class_mop)
{
  DB_VALUE val;
  int error_code = NO_ERROR;
  DB_OBJECT *obj = db_create_internal (class_mop);
  const char *dummy = "X";
  if (obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }
  error_code = db_make_varchar (&val, 1, dummy, strlen (dummy), LANG_SYS_CODESET, LANG_SYS_COLLATION);
  if (error_code == NO_ERROR)
    {
      error_code = db_put_internal (obj, CT_DUAL_DUMMY, &val);
    }

  return error_code;
}

int
catcls_add_charsets (struct db_object *class_mop)
{
  int i;

  for (i = INTL_CODESET_BINARY; i <= INTL_CODESET_LAST; i++)
    {
      DB_OBJECT *obj;
      DB_VALUE val;
      char *charset_name;

      obj = db_create_internal (class_mop);
      if (obj == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      db_make_int (&val, i);
      db_put_internal (obj, CT_DBCHARSET_CHARSET_ID, &val);

      charset_name = (char *) lang_charset_cubrid_name ((INTL_CODESET) i);
      if (charset_name == NULL)
	{
	  return ER_LANG_CODESET_NOT_AVAILABLE;
	}

      db_make_varchar (&val, 32, charset_name, strlen (charset_name), LANG_SYS_CODESET, LANG_SYS_COLLATION);
      db_put_internal (obj, CT_DBCHARSET_CHARSET_NAME, &val);

      db_make_int (&val, LANG_GET_BINARY_COLLATION (i));
      db_put_internal (obj, CT_DBCHARSET_DEFAULT_COLLATION, &val);

      db_make_int (&val, INTL_CODESET_MULT (i));
      db_put_internal (obj, CT_DBCHARSET_CHAR_SIZE, &val);
    }

  return NO_ERROR;
}

/* ========================================================================== */
/* MAIN APIS */
/* ========================================================================== */

static std::vector<cubschema::catcls_function> clist;
static std::vector<cubschema::catcls_function> vclist;

void
catcls_init (void)
{
  using namespace cubschema;

  // TODO: for late initialization (for au_init () to retrieve MOPs: Au_dba_user and Au_public_user)
#define ADD_TABLE_DEFINITION(name,def) clist.emplace_back(name, def)
#define ADD_VIEW_DEFINITION(name,def) vclist.emplace_back(name, def)

  ADD_TABLE_DEFINITION (CT_CLASS_NAME, system_catalog_initializer::get_class ());
  ADD_TABLE_DEFINITION (CT_ATTRIBUTE_NAME, system_catalog_initializer::get_attribute ());
  ADD_TABLE_DEFINITION (CT_DOMAIN_NAME, system_catalog_initializer::get_domain ());
  ADD_TABLE_DEFINITION (CT_METHOD_NAME, system_catalog_initializer::get_method ());
  ADD_TABLE_DEFINITION (CT_METHSIG_NAME, system_catalog_initializer::get_meth_sig ());
  ADD_TABLE_DEFINITION (CT_METHARG_NAME, system_catalog_initializer::get_meth_arg ());
  ADD_TABLE_DEFINITION (CT_METHFILE_NAME, system_catalog_initializer::get_meth_file ());
  ADD_TABLE_DEFINITION (CT_QUERYSPEC_NAME, system_catalog_initializer::get_query_spec ());
  ADD_TABLE_DEFINITION (CT_INDEX_NAME, system_catalog_initializer::get_index ());
  ADD_TABLE_DEFINITION (CT_INDEXKEY_NAME, system_catalog_initializer::get_index_key ());
  ADD_TABLE_DEFINITION (CT_CLASSAUTH_NAME, system_catalog_initializer::get_auth ());
  ADD_TABLE_DEFINITION (CT_TRIGGER_NAME, system_catalog_initializer::get_trigger ());
  ADD_TABLE_DEFINITION (CT_PARTITION_NAME, system_catalog_initializer::get_partition ());
  ADD_TABLE_DEFINITION (CT_DATATYPE_NAME, system_catalog_initializer::get_data_type ());
  ADD_TABLE_DEFINITION (CT_STORED_PROC_NAME, system_catalog_initializer::get_stored_procedure ());
  ADD_TABLE_DEFINITION (CT_STORED_PROC_ARGS_NAME, system_catalog_initializer::get_stored_procedure_args ());
  ADD_TABLE_DEFINITION (CT_STORED_PROC_CODE_NAME, system_catalog_initializer::get_stored_procedure_code ());
  ADD_TABLE_DEFINITION (CT_SERIAL_NAME, system_catalog_initializer::get_serial ());
  ADD_TABLE_DEFINITION (CT_HA_APPLY_INFO_NAME, system_catalog_initializer::get_ha_apply_info ());
  ADD_TABLE_DEFINITION (CT_COLLATION_NAME, system_catalog_initializer::get_collation ());
  ADD_TABLE_DEFINITION (CT_CHARSET_NAME, system_catalog_initializer::get_charset ());
  ADD_TABLE_DEFINITION (CT_DUAL_NAME, system_catalog_initializer::get_dual ());
  ADD_TABLE_DEFINITION (CT_SYNONYM_NAME, system_catalog_initializer::get_synonym ());
  ADD_TABLE_DEFINITION (CT_SERVER_NAME, system_catalog_initializer::get_server ());
  ADD_TABLE_DEFINITION (CT_HISTOGRAM_NAME, system_catalog_initializer::get_histogram());
  ADD_TABLE_DEFINITION (CT_GLOBAL_TRAN_NAME, system_catalog_initializer::get_global_tran ());

  ADD_VIEW_DEFINITION (CTV_CLASS_NAME, system_catalog_initializer::get_view_class ());
  ADD_VIEW_DEFINITION (CTV_SUPER_CLASS_NAME, system_catalog_initializer::get_view_direct_super_class ());
  ADD_VIEW_DEFINITION (CTV_VCLASS_NAME, system_catalog_initializer::get_view_vclass ());
  ADD_VIEW_DEFINITION (CTV_ATTRIBUTE_NAME, system_catalog_initializer::get_view_attribute ());
  ADD_VIEW_DEFINITION (CTV_ATTR_SD_NAME, system_catalog_initializer::get_view_attr_setdomain_elm ());
  ADD_VIEW_DEFINITION (CTV_METHOD_NAME, system_catalog_initializer::get_view_method ());
  ADD_VIEW_DEFINITION (CTV_METHARG_NAME, system_catalog_initializer::get_view_meth_arg ());
  ADD_VIEW_DEFINITION (CTV_METHARG_SD_NAME, system_catalog_initializer::get_view_meth_arg_setdomain_elm ());
  ADD_VIEW_DEFINITION (CTV_METHFILE_NAME, system_catalog_initializer::get_view_meth_file ());
  ADD_VIEW_DEFINITION (CTV_INDEX_NAME, system_catalog_initializer::get_view_index ());
  ADD_VIEW_DEFINITION (CTV_INDEXKEY_NAME, system_catalog_initializer::get_view_index_key ());
  ADD_VIEW_DEFINITION (CTV_AUTH_NAME, system_catalog_initializer::get_view_auth ());
  ADD_VIEW_DEFINITION (CTV_TRIGGER_NAME, system_catalog_initializer::get_view_trigger ());
  ADD_VIEW_DEFINITION (CTV_PARTITION_NAME, system_catalog_initializer::get_view_partition ());
  ADD_VIEW_DEFINITION (CTV_STORED_PROC_NAME, system_catalog_initializer::get_view_stored_procedure ());
  ADD_VIEW_DEFINITION (CTV_STORED_PROC_ARGS_NAME, system_catalog_initializer::get_view_stored_procedure_args ());
  ADD_VIEW_DEFINITION (CTV_SERIAL_NAME, system_catalog_initializer::get_view_serial ());
  ADD_VIEW_DEFINITION (CTV_HA_APPLY_INFO_NAME, system_catalog_initializer::get_view_ha_apply_info ());
  ADD_VIEW_DEFINITION (CTV_COLLATION_NAME, system_catalog_initializer::get_view_collation ());
  ADD_VIEW_DEFINITION (CTV_USER_NAME, system_catalog_initializer::get_view_user ());
  ADD_VIEW_DEFINITION (CTV_AUTHORIZATION_NAME, system_catalog_initializer::get_view_authorization ());
  ADD_VIEW_DEFINITION (CTV_CHARSET_NAME, system_catalog_initializer::get_view_charset ());
  ADD_VIEW_DEFINITION (CTV_SERVER_NAME, system_catalog_initializer::get_view_server ());
  ADD_VIEW_DEFINITION (CTV_SYNONYM_NAME, system_catalog_initializer::get_view_synonym ());
  ADD_VIEW_DEFINITION (CTV_HISTOGRAM_NAME, system_catalog_initializer::get_view_db_histogram ());
}

int
catcls_install (void)
{
  int error_code = NO_ERROR;
  const size_t num_classes = clist.size ();
  const size_t num_vclasses = vclist.size ();
  std::vector<MOP> class_mop (num_classes, nullptr);
  int save;
  size_t i;
  AU_DISABLE (save);

  using catalog_builder = cubschema::system_catalog_builder;

  au_set_user (Au_dba_user); // change to dba user

  for (i = 0; i < num_classes; i++)
    {
      // new routine
      class_mop[i] = catalog_builder::create_and_mark_system_class (clist[i].name);
      if (class_mop[i] == nullptr)
	{
	  assert (false);
	  error_code = er_errid ();
	  goto end;
	}
    }

  for (i = 0; i < num_classes; i++)
    {
      // new routine
      error_code = catalog_builder::build_class (class_mop[i], clist[i].definition);
      if (error_code != NO_ERROR)
	{
	  assert (false);
	  goto end;
	}
    }

  for (i = 0; i < num_vclasses; i++)
    {
      // new routine
      MOP class_mop = catalog_builder::create_and_mark_system_class (vclist[i].name);
      if (class_mop != nullptr)
	{
	  error_code = catalog_builder::build_vclass (class_mop, vclist[i].definition);
	}

      error_code = er_errid();
      if (error_code != NO_ERROR)
	{
	  goto end;
	}
    }

end:
  AU_ENABLE (save);

  clist.clear ();
  vclist.clear ();

  return error_code;
}

namespace cubschema
{
  /* ========================================================================== */
  /* NEW DEFINITION (CLASS) */
  /* ========================================================================== */

  system_catalog_definition
  system_catalog_initializer::get_class ()
  {
    return system_catalog_definition (
		   // name
		   CT_CLASS_NAME,
		   // columns
    {
      {"class_of", "object"},
      {"unique_name", format_varchar (255)},
      {"class_name", format_varchar (255)},
      {"class_type", "integer"},
      {"is_system_class", "integer"},
      {"owner", AU_USER_CLASS_NAME},
      {"inst_attr_count", "integer"},
      {"class_attr_count", "integer"},
      {"shared_attr_count", "integer"},
      {"inst_meth_count", "integer"},
      {"class_meth_count", "integer"},
      {"collation_id", "integer"},
      {"tde_algorithm", "integer"},
      {"statistics_strategy", "integer"},
      {"flags", "integer"},
      {"sub_classes", format_sequence (CT_CLASS_NAME)},
      {"super_classes", format_sequence (CT_CLASS_NAME)},
      {"inst_attrs", format_sequence (CT_ATTRIBUTE_NAME)},
      {"class_attrs", format_sequence (CT_ATTRIBUTE_NAME)},
      {"shared_attrs", format_sequence (CT_ATTRIBUTE_NAME)},
      {"inst_meths", format_sequence (CT_METHOD_NAME)},
      {"class_meths", format_sequence (CT_METHOD_NAME)},
      {"meth_files", format_sequence (CT_METHFILE_NAME)},
      {"query_specs", format_sequence (CT_QUERYSPEC_NAME)},
      {"indexes", format_sequence (CT_INDEX_NAME)},
      {"comment", format_varchar (2048)},
      {"partition", format_sequence (CT_PARTITION_NAME)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      {"checked_time", "datetime"}
    },
// constraints
    {
      /*
      *  Define the index name so that it always has the same name as the macro variable (CATCLS_INDEX_NAME)
      *  in src/storage/catalog_class.c.
      *
      *  _db_class must not have a primary key or a unique index. In the btree_key_insert_new_key function
      *  in src/storage/btree.c, it becomes assert (false) in the code below.
      *
      *    CREATE TABLE t1 (c1 INT);
      *    RENAME CLASS t1 AS t2;
      *
      *    assert ((btree_is_online_index_loading (insert_helper->purpose)) || !BTREE_IS_UNIQUE (btid_int->unique_pk)
      *            || log_is_in_crash_recovery () || btree_check_locking_for_insert_unique (thread_p, insert_helper));
      *
      *  All others should be false, and !BTREE_IS_UNIQUE (btid_int->unique_pk) should be true. However,
      *  if there is a primary key or a unique index, !BTREE_IS_UNIQUE (btid_int->unique_pk) also becomes false,
      *  and all are false. In the btree_key_insert_new_key function, analysis should be added to the operation
      *  of the primary key and unique index.
      *
      *  Currently, it is solved by creating only general indexes, not primary keys or unique indexes.
      */
      {DB_CONSTRAINT_INDEX, "i__db_class_unique_name", {"unique_name", nullptr}, false},
      {DB_CONSTRAINT_INDEX, "", {"class_name", "owner", nullptr}, false},
      {DB_CONSTRAINT_INDEX, "", {"class_of", nullptr}, false}
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_attribute ()
  {
    return system_catalog_definition (
		   // name
		   CT_ATTRIBUTE_NAME,
		   // columns
    {
      {"class_of", CT_CLASS_NAME},
      {"attr_name", format_varchar (255)},
      {"attr_type", "integer"},
      {"from_class_of", CT_CLASS_NAME},
      {"from_attr_name", format_varchar (255)},
      {"def_order", "integer"},
      {"data_type", "integer"},
      {"default_value", format_varchar (DB_MAX_DEFAULT_EXPR_LENGTH)},
      {"domains", format_sequence (CT_DOMAIN_NAME)},
      {"is_nullable", "integer"},
      {"flags", "integer"},
      {"comment", format_varchar (2048)}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"class_of", "attr_name", "attr_type", nullptr}, false}
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_domain ()
  {
    return system_catalog_definition (
		   // name
		   CT_DOMAIN_NAME,
		   // columns
    {
      {"object_of", "object"},
      {"data_type", "integer"},
      {"prec", "integer"},
      {"scale", "integer"},
      {"class_of", CT_CLASS_NAME},
      {"code_set", "integer"},
      {"collation_id", "integer"},
      {"enumeration", format_sequence ("character varying")},
      {"set_domains", format_sequence (CT_DOMAIN_NAME)},
      {"json_schema", "string"},
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"object_of", nullptr}, false},
      {DB_CONSTRAINT_INDEX, "", {"data_type", nullptr}, false}
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_method ()
  {
    return system_catalog_definition (
		   // name
		   CT_METHOD_NAME,
		   // columns
    {
      {"class_of", CT_CLASS_NAME},
      {"meth_name", format_varchar (255)},
      {"meth_type", "integer"},
      {"from_class_of", CT_CLASS_NAME},
      {"from_meth_name", format_varchar (255)},
      {"signatures", format_sequence (CT_METHSIG_NAME)}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"class_of", "meth_name", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_meth_sig ()
  {
    return system_catalog_definition (
		   // name
		   CT_METHSIG_NAME,
		   // columns
    {
      {"meth_of", CT_METHOD_NAME},
      {"func_name", format_varchar (255)},
      {"arg_count", "integer"},
      {"return_value", format_sequence (CT_METHARG_NAME)},
      {"arguments", format_sequence (CT_METHARG_NAME)}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"meth_of", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_meth_arg ()
  {
    return system_catalog_definition (
		   // name
		   CT_METHARG_NAME,
		   // columns
    {
      {"meth_sig_of", CT_METHSIG_NAME},
      {"data_type", "integer"},
      {"index_of", "integer"},
      {"domains", format_sequence (CT_DOMAIN_NAME)}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"meth_sig_of", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_meth_file ()
  {
    return system_catalog_definition (
		   // name
		   CT_METHFILE_NAME,
		   // columns
    {
      {"class_of", CT_CLASS_NAME},
      {"from_class_of", CT_CLASS_NAME},
      {"path_name", format_varchar (255)}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"class_of", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_query_spec ()
  {
    return system_catalog_definition (
		   // name
		   CT_QUERYSPEC_NAME,
		   // columns
    {
      {"class_of", CT_CLASS_NAME},
      {"spec", format_varchar (1073741823)},
      {"invalidated_time", "datetime"}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"class_of", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_index ()
  {
    return system_catalog_definition (
		   // name
		   CT_INDEX_NAME,
		   // columns
    {
      {"class_of", CT_CLASS_NAME},
      {"index_name", format_varchar (255)},
      {"is_unique", "integer"},
      {"key_count", "integer"},
      {"key_attrs", format_sequence (CT_INDEXKEY_NAME)},
      {"is_reverse", "integer"},
      {"is_primary_key", "integer"},
      {"is_foreign_key", "integer"},
      {"filter_expression", format_varchar (1073741823)},
      {"have_function", "integer"},
      {"status", "integer"},
      {"referential_index", CT_INDEX_NAME},
      {"delete_rule", "integer"},
      {"update_rule", "integer"},
      {"referential_match_option", "integer"},
      {"index_type", "integer"},
      {"options", "integer"},
      {"comment", format_varchar (1024)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"class_of", nullptr}, false},
      {DB_CONSTRAINT_INDEX, "", {"index_name", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_index_key ()
  {
    return system_catalog_definition (
		   // name
		   CT_INDEXKEY_NAME,
		   // columns
    {
      {"index_of", CT_INDEX_NAME},
      {"key_attr_name", format_varchar (255)},
      {"key_order", "integer"},
      {"asc_desc", "integer"},
      {"key_prefix_length", "integer", make_int_value_fn (-1)},
      {"func", format_varchar (1023)}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"index_of", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_auth ()
  {
    return system_catalog_definition (
		   // name
		   CT_CLASSAUTH_NAME,
		   // columns
    {
      {"grantor", AU_USER_CLASS_NAME},
      {"grantee", AU_USER_CLASS_NAME},
      {"object_type", "integer"},
      {"object_of", "object"},
      {"auth_type", format_varchar (7)},
      {"is_grantable", "integer"},
      {"created_time", "datetime"},
      {"updated_time", "datetime"}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"grantee", nullptr}, false},
      {DB_CONSTRAINT_INDEX, "", {"object_of", nullptr}, false}
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializers
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_trigger ()
  {
    return system_catalog_definition (
		   // name
		   CT_TRIGGER_NAME,
		   // columns
    {
      {TR_ATT_UNIQUE_NAME, "string"},
      {TR_ATT_OWNER, AU_USER_CLASS_NAME},
      {TR_ATT_NAME, "string"},
      {TR_ATT_STATUS, "integer", make_int_value_fn (TR_STATUS_ACTIVE)},
      {TR_ATT_PRIORITY, "double", make_double_value_fn (TR_LOWEST_PRIORITY)},
      {TR_ATT_EVENT, "integer", make_int_value_fn (TR_EVENT_NULL)},
      {TR_ATT_CLASS, "object"},
      {TR_ATT_ATTRIBUTE, "string"},
      {TR_ATT_CLASS_ATTRIBUTE, "integer", make_int_value_fn (0)},
      {TR_ATT_CONDITION_TYPE, "integer"},
      {TR_ATT_CONDITION, "string"},
      {TR_ATT_CONDITION_TIME, "integer"},
      {TR_ATT_ACTION_TYPE, "integer"},
      {TR_ATT_ACTION, "string"},
      {TR_ATT_ACTION_TIME, "integer"},
      {TR_ATT_COMMENT, format_varchar (1024)},
      {TR_ATT_CREATED_TIME, "datetime"},
      {TR_ATT_UPDATED_TIME, "datetime"}
    },
// constraints
    {},
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializers
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_partition ()
  {
    return system_catalog_definition (
		   // name
		   CT_PARTITION_NAME,
		   // columns
    {
      {"class_of", CT_CLASS_NAME},
      {"pname", format_varchar (255)},
      {"ptype", "integer"},
      {"pexpr", format_varchar (2048)},
      {"pvalues", format_sequence ("")},
      {"class_partition_type", "integer"},
      {"comment", format_varchar (1024)},
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"class_of", "pname", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_data_type ()
  {
    return system_catalog_definition (
		   // name
		   CT_DATATYPE_NAME,
		   // columns
    {
      {"type_id", "integer"},
      {"type_name", format_varchar (16)}
    },
// constraints
    {
      {DB_CONSTRAINT_PRIMARY_KEY, "", {"type_id", "type_name", nullptr}, false}
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    catcls_add_data_type
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_stored_procedure ()
  {
    return system_catalog_definition (
		   // name
		   CT_STORED_PROC_NAME,
		   // columns
    {
      {SP_ATTR_UNIQUE_NAME, format_varchar (255)},
      {SP_ATTR_SP_NAME, format_varchar (255)},
      {SP_ATTR_SP_TYPE, "integer"},
      {SP_ATTR_RETURN_TYPE, "integer"},
      {SP_ATTR_ARG_COUNT, "integer"},
      {SP_ATTR_ARGS, format_sequence (CT_STORED_PROC_ARGS_NAME)},
      {SP_ATTR_LANG, "integer"},
      {SP_ATTR_PKG_NAME, format_varchar (255)},
      {SP_ATTR_IS_SYSTEM_GENERATED, "integer"},
      {SP_ATTR_DIRECTIVE, "integer"},
      {SP_ATTR_TARGET_CLASS, format_varchar (1024)},
      {SP_ATTR_TARGET_METHOD, format_varchar (SP_ATTR_TARGET_METHOD_LEN)},
      {SP_ATTR_OWNER, AU_USER_CLASS_NAME},
      {SP_ATTR_SQL_DATA_ACCESS, "integer"},
      {SP_ATTR_COMMENT, format_varchar (1024)},
      {SP_ATTR_CREATED_TIME, "datetime"},
      {SP_ATTR_UPDATED_TIME, "datetime"},
    },
// constraints
    {
      {DB_CONSTRAINT_PRIMARY_KEY, "pk_db_stored_procedure_unique_name", {"unique_name", nullptr}, false}
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_stored_procedure_args ()
  {
    return system_catalog_definition (
		   // name
		   CT_STORED_PROC_ARGS_NAME,
		   // columns
    {
      {SP_ARG_ATTR_SP_OF, CT_STORED_PROC_NAME},
      {SP_ARG_ATTR_INDEX_OF, "integer"},
      {SP_ARG_ATTR_IS_SYSTEM_GENERATED, "integer"},
      {SP_ARG_ATTR_ARG_NAME, format_varchar (255)},
      {SP_ARG_ATTR_DATA_TYPE, "integer"},
      {SP_ARG_ATTR_MODE, "integer"},
      {SP_ARG_ATTR_DEFAULT_VALUE, format_varchar (DB_MAX_DEFAULT_EXPR_LENGTH)}, // TODO: CBRD-25261
      {SP_ARG_ATTR_IS_OPTIONAL, "integer"}, // default_value is used only when is_optional is 1
      {SP_ARG_ATTR_COMMENT, format_varchar (1024)},
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"sp_of", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_stored_procedure_code ()
  {
    return system_catalog_definition (
		   // name
		   CT_STORED_PROC_CODE_NAME,
		   // columns
    {
      {SP_CODE_ATTR_NAME, format_varchar (1024)}, // same with [_db_stored_procedure].[target_class]
      {SP_CODE_ATTR_CREATED_TIME, format_varchar (16)},
      {SP_CODE_ATTR_OWNER, AU_USER_CLASS_NAME},
      {SP_CODE_ATTR_IS_STATIC, "integer"},
      {SP_CODE_ATTR_IS_SYSTEM_GENERATED, "integer"},
      {SP_CODE_ATTR_STYPE, "integer"},
      {SP_CODE_ATTR_SCODE, format_varchar (1073741823)},
      {SP_CODE_ATTR_OTYPE, "integer"},
      {SP_CODE_ATTR_OCODE, format_varchar (1073741823)}
    },
// constraints
    {
      {DB_CONSTRAINT_PRIMARY_KEY, "", {"name", nullptr}, false}
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_serial ()
  {
    return system_catalog_definition (
		   // name
		   CT_SERIAL_NAME,
		   // columns
    {
      {"unique_name", "string"},
      {"name", "string"},
      {"owner", AU_USER_CLASS_NAME},
      {"current_val", format_numeric (DB_MAX_FIXED_NUMERIC_PRECISION, 0), make_numeric_value_fn ("1")},
      {"increment_val", format_numeric (DB_MAX_FIXED_NUMERIC_PRECISION, 0), make_numeric_value_fn ("1")},
      {"max_val", format_numeric (DB_MAX_FIXED_NUMERIC_PRECISION, 0)},
      {"min_val", format_numeric (DB_MAX_FIXED_NUMERIC_PRECISION, 0)},
      {"start_val", format_numeric (DB_MAX_FIXED_NUMERIC_PRECISION, 0), make_numeric_value_fn ("1")},
      {"cyclic", "integer", make_int_value_fn (0)},
      {"started", "integer", make_int_value_fn (0)},
      {"class_name", "string"},
      {"attr_name", "string"},
      {attribute_kind::CLASS_METHOD, "change_serial_owner", "au_change_serial_owner_method"},
      {"cached_num", "integer",	make_int_value_fn (0)},
      {"comment", format_varchar (1024)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"}
    },
// constraints
    {
      {DB_CONSTRAINT_PRIMARY_KEY, "pk_db_serial_unique_name", {"unique_name", nullptr}, false},
      {DB_CONSTRAINT_UNIQUE, "", {"name", "owner", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"current_val", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"increment_val", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"max_val", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"min_val", nullptr}, false}
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_ha_apply_info ()
  {
    return system_catalog_definition (
		   // name
		   CT_HA_APPLY_INFO_NAME,
		   // columns
    {
      {"db_name", format_varchar (255)},
      {"db_creation_time", "datetime"},
      {"copied_log_path", format_varchar (4096)},
      {"committed_lsa_pageid", "bigint"},
      {"committed_lsa_offset", "integer"},
      {"committed_rep_pageid", "bigint"},
      {"committed_rep_offset", "integer"},
      {"append_lsa_pageid", "bigint"},
      {"append_lsa_offset", "integer"},
      {"eof_lsa_pageid", "bigint"},
      {"eof_lsa_offset", "integer"},
      {"final_lsa_pageid", "bigint"},
      {"final_lsa_offset", "integer"},
      {"required_lsa_pageid", "bigint"},
      {"required_lsa_offset", "integer"},
      {"log_record_time", "datetime"},
      {"log_commit_time", "datetime"},
      {"last_access_time", "datetime"},
      {"status", "integer"},
      {"insert_counter", "bigint"},
      {"update_counter", "bigint"},
      {"delete_counter", "bigint"},
      {"schema_counter", "bigint"},
      {"commit_counter", "bigint"},
      {"fail_counter", "bigint"},
      {"start_time", "datetime"}
    },
// constraints
    {
      {DB_CONSTRAINT_UNIQUE, "", {"db_name", "copied_log_path", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"db_name", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"copied_log_path", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"committed_lsa_pageid", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"committed_lsa_offset", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"required_lsa_pageid", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"required_lsa_offset", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_collation ()
  {
    return system_catalog_definition (
		   // name
		   CT_COLLATION_NAME,
		   // columns
    {
      {CT_DBCOLL_COLL_ID_COLUMN, "integer"},
      {CT_DBCOLL_COLL_NAME_COLUMN, format_varchar (32)},
      {CT_DBCOLL_CHARSET_ID_COLUMN, "integer"},
      {CT_DBCOLL_BUILT_IN_COLUMN, "integer"},
      {CT_DBCOLL_EXPANSIONS_COLUMN, "integer"},
      {CT_DBCOLL_CONTRACTIONS_COLUMN, "integer"},
      {CT_DBCOLL_UCA_STRENGTH, "integer"},
      {CT_DBCOLL_CHECKSUM_COLUMN, format_varchar (32)}
    },
// constraints
    {
      {DB_CONSTRAINT_PRIMARY_KEY, "", {CT_DBCOLL_COLL_ID_COLUMN, nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    catcls_add_collations
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_charset ()
  {
    return system_catalog_definition (
		   // name
		   CT_CHARSET_NAME,
		   // columns
    {
      {CT_DBCHARSET_CHARSET_ID, "integer"},
      {CT_DBCHARSET_CHARSET_NAME, format_varchar (32)},
      {CT_DBCHARSET_DEFAULT_COLLATION, "integer"},
      {CT_DBCHARSET_CHAR_SIZE, "integer"}
    },
// constraints
    {},
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    catcls_add_charsets
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_dual ()
  {
    return system_catalog_definition (
		   // name
		   CT_DUAL_NAME,
		   // columns
    {
      {CT_DUAL_DUMMY, format_varchar (1)}
    },
// constraints
    {},
// authorization
    {
      // owner, grants
      Au_dba_user,
      {
	{Au_public_user, AU_SELECT, false},
	{Au_information_schema_user, AU_SELECT, false}
      }
    },
// initializer
    catcls_add_dual
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_synonym ()
  {
    return system_catalog_definition (
		   // name
		   CT_SYNONYM_NAME,
		   // columns
    {
      {"unique_name", format_varchar (255)},
      {"name", format_varchar (255)},
      {"owner", AU_USER_CLASS_NAME},
      {"is_public", "integer", make_int_value_fn (0)},
      {"target_unique_name", format_varchar (255)},
      {"target_name", format_varchar (255)},
      {"target_owner", AU_USER_CLASS_NAME},
      {"comment", format_varchar (2048)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"}
    },
// constraints
    {
      {DB_CONSTRAINT_PRIMARY_KEY, "", {"unique_name", nullptr}, false},
      {DB_CONSTRAINT_INDEX, "", {"name", "owner", "is_public", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"name", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"owner", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"is_public", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"target_unique_name", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"target_name", nullptr}, false},
      {DB_CONSTRAINT_NOT_NULL, "", {"target_owner", nullptr}, false}
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_server ()
  {
    return system_catalog_definition (
		   // name
		   CT_SERVER_NAME,
		   // columns
    {
      {"link_name", format_varchar (255)},
      {"host", format_varchar (255)},
      {"port", "integer"},
      {"db_name", format_varchar (255)},
      /* dblink remote user_name; kept 255 for external DBMS compatibility */
      {"user_name", format_varchar (255)},
      {"password", "string"},
      {"properties", format_varchar (2048)},
      {"owner", AU_USER_CLASS_NAME},
      {"comment", format_varchar (1024)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      {"invalidated_time", "datetime"}
    },
// constraints
    {
      {DB_CONSTRAINT_PRIMARY_KEY, "", {"link_name", "owner", nullptr}, false}
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {{Au_information_schema_user, AU_SELECT, false}}
    },
// initializer
    nullptr
	   );


  }

  system_catalog_definition
  system_catalog_initializer::get_histogram ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CT_HISTOGRAM_NAME,
		   // columns
    {
      {"class_of", "object"},
      {"key_attr", format_varchar (255)},
      {"with_fullscan","integer"},
      {"null_frequency", "double"},
      {"histogram_values", format_varbit (1073741823) }
    },
// constraint
    {
      {DB_CONSTRAINT_UNIQUE, "", {"class_of", "key_attr", nullptr}, false}
    },
// authorization
    {
      // owner
      Au_dba_user, {}
    },
// initializer
    nullptr
	   );

  }

  system_catalog_definition
  system_catalog_initializer::get_global_tran ()
  {
    return system_catalog_definition (
		   // name
		   CT_GLOBAL_TRAN_NAME,
		   // columns
    {
      {"gtrid", "integer"},
      {"bqual", "integer"},
      {"conn_url", format_varchar (512)},
      {"user_name", format_varchar (32)},
      {"password", format_varchar (256)},
      {"state", format_varchar (1)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"}
    },
// constraints
    {},
// authorization
    {
      Au_dba_user, {}
    },
// initializer
    nullptr
	   );
  }

  /* ========================================================================== */
  /* NEW DEFINITION (VCLASS) */
  /* ========================================================================== */

  system_catalog_definition
  system_catalog_initializer::get_view_class ()
  {
    return system_catalog_definition (
		   // name
		   CTV_CLASS_NAME,
		   // columns
    {
      {"class_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"class_type", format_varchar (6)},
      {"is_system_class", format_varchar (3)},
      {"tde_algorithm", format_varchar (32)},
      {"statistics_strategy", format_varchar (8)},
      {"partitioned", format_varchar (3)},
      {"is_reuse_oid_class", format_varchar (3)},
      {"collation", format_varchar (32)},
      {"comment", format_varchar (2048)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      {"checked_time", "datetime"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_class_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_direct_super_class ()
  {
    return system_catalog_definition (
		   // name
		   CTV_SUPER_CLASS_NAME,
		   // columns
    {
      {"class_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"super_class_name", format_varchar (255)},
      {"super_owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_direct_super_class_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_vclass ()
  {
    return system_catalog_definition (
		   // name
		   CTV_VCLASS_NAME,
		   // columns
    {
      {"vclass_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"vclass_def", format_varchar (1073741823)},
      {"comment", format_varchar (2048)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      {"invalidated_time", "datetime"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_vclass_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_attribute ()
  {
    return system_catalog_definition (
		   // name
		   CTV_ATTRIBUTE_NAME,
		   // columns
    {
      {"attr_name", format_varchar (255)},
      {"class_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"attr_type", format_varchar (8)},
      {"def_order", "integer"},
      {"from_class_name", format_varchar (255)},
      {"from_owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"from_attr_name", format_varchar (255)},
      {"data_type", format_varchar (9)},
      {"prec", "integer"},
      {"scale", "integer"},
      {"charset", format_varchar (32)},
      {"collation", format_varchar (32)},
      {"domain_class_name", format_varchar (255)},
      {"domain_owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"default_value", format_varchar (DB_MAX_DEFAULT_EXPR_LENGTH)},
      {"is_partition_key", format_varchar (3)},
      {"is_nullable", format_varchar (3)},
      {"is_invisible", format_varchar (3)},
      {"comment", format_varchar (1024)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_attribute_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_attr_setdomain_elm ()
  {
    return system_catalog_definition (
		   // name
		   CTV_ATTR_SD_NAME,
		   // columns
    {
      {"attr_name", format_varchar (255)},
      {"class_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"attr_type", format_varchar (8)},
      {"data_type", format_varchar (9)},
      {"prec", "integer"},
      {"scale", "integer"},
      {"code_set", "integer"},
      {"domain_class_name", format_varchar (255)},
      {"domain_owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_attr_setdomain_elm_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_method ()
  {
    return system_catalog_definition (
		   // name
		   CTV_METHOD_NAME,
		   // columns
    {
      {"meth_name", format_varchar (255)},
      {"class_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"meth_type", format_varchar (8)},
      {"from_class_name", format_varchar (255)},
      {"from_owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"from_meth_name", format_varchar (255)},
      {"func_name", format_varchar (255)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_method_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_meth_arg ()
  {
    return system_catalog_definition (
		   // name
		   CTV_METHARG_NAME,
		   // columns
    {
      {"meth_name", format_varchar (255)},
      {"class_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"meth_type", format_varchar (8)},
      {"index_of", "integer"},
      {"data_type", format_varchar (9)},
      {"prec", "integer"},
      {"scale", "integer"},
      {"code_set", "integer"},
      {"domain_class_name", format_varchar (255)},
      {"domain_owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_method_arg_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_meth_arg_setdomain_elm ()
  {
    return system_catalog_definition (
		   // name
		   CTV_METHARG_SD_NAME,
		   // columns
    {
      {"meth_name", format_varchar (255)},
      {"class_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"meth_type", format_varchar (8)},
      {"index_of", "integer"},
      {"data_type", format_varchar (9)},
      {"prec", "integer"},
      {"scale", "integer"},
      {"code_set", "integer"},
      {"domain_class_name", format_varchar (255)},
      {"domain_owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_meth_arg_setdomain_elm_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_meth_file ()
  {
    return system_catalog_definition (
		   // name
		   CTV_METHFILE_NAME,
		   // columns
    {
      {"class_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"path_name", format_varchar (255)},
      {"from_class_name", format_varchar (255)},
      {"from_owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_meth_file_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_index ()
  {
    return system_catalog_definition (
		   // name
		   CTV_INDEX_NAME,
		   // columns
    {
      {"index_name", format_varchar (255)},
      {"is_unique", format_varchar (3)},
      {"is_reverse", format_varchar (3)},
      {"class_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"key_count", "integer"},
      {"is_primary_key", format_varchar (3)},
      {"is_foreign_key", format_varchar (3)},
      {"filter_expression", format_varchar (1073741823)},
      {"have_function", format_varchar (3)},
      {"status", format_varchar (255)},
      {"referential_index_class_owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"referential_index_class_name", format_varchar (255)},
      {"referential_index_name", format_varchar (255)},
      {"delete_rule", format_varchar (32)},
      {"update_rule", format_varchar (32)},
      {"referential_match_option", format_varchar (7)},
      {"index_type", format_varchar (32)},
      {"deduplicate_key_level", "integer"},
      {"comment", format_varchar (1024)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_index_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_index_key ()
  {
    return system_catalog_definition (
		   // name
		   CTV_INDEXKEY_NAME,
		   // columns
    {
      {"index_name", format_varchar (255)},
      {"class_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"key_attr_name", format_varchar (255)},
      {"key_order", "integer"},
      {"asc_desc", format_varchar (4)},
      {"key_prefix_length", "integer"},
      {"func", format_varchar (1023)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_index_key_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  /* When a user is granted SELECT privilege,
   * that user can also view the list of privileges that other users have been granted.
   * Is this no problem? */

  system_catalog_definition
  system_catalog_initializer::get_view_auth ()
  {
    return system_catalog_definition (
		   // name
		   CTV_AUTH_NAME,
		   // columns
    {
      {"grantor_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"grantee_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"object_type", format_varchar (16)},
      {"object_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"auth_type", format_varchar (7)},
      {"is_grantable", format_varchar (3)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_auth_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_trigger ()
  {
    return system_catalog_definition (
		   // name
		   CTV_TRIGGER_NAME,
		   // columns
    {
      {"trigger_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"target_class_name", format_varchar (255)},
      {"target_owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"target_attr_name", format_varchar (255)},
      {"target_attr_type", format_varchar (8)},
      {"action_type", "integer"},
      {"action_time", "integer"},
      {"comment", format_varchar (1024)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_trigger_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_partition ()
  {
    return system_catalog_definition (
		   // name
		   CTV_PARTITION_NAME,
		   // columns
    {
      {"class_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"partition_name", format_varchar (255)},
      {"partition_class_name", format_varchar (255)},
      {"partition_type", format_varchar (32)},
      {"partition_expr", format_varchar (2048)},
      {"partition_values", "sequence of"},
      {"class_partition_type", format_varchar (32)},
      {"comment", format_varchar (1024)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_partition_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_stored_procedure ()
  {
    return system_catalog_definition (
		   // name
		   CTV_STORED_PROC_NAME,
		   // columns
    {
      {"sp_name", format_varchar (255)},
      {"pkg_name", format_varchar (255)},
      {"sp_type", format_varchar (16)},
      {"return_type", format_varchar (16)},
      {"arg_count", "integer"},
      {"lang", format_varchar (16)},
      {"authid", format_varchar (16)},
      {"is_deterministic", format_varchar (3)},
      {"target", format_varchar (4096)},
      {"owner", format_varchar (DB_MAX_USER_LENGTH)},
      {"code", format_varchar (1073741823)},
      {"sql_data_access", format_varchar (17)},
      {"comment", format_varchar (1024)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_stored_procedure_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_stored_procedure_args ()
  {
    return system_catalog_definition (
		   // name
		   CTV_STORED_PROC_ARGS_NAME,
		   // columns
    {
      {"sp_name", format_varchar (255)},
      {"owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"pkg_name", format_varchar (255)},
      {"index_of", "integer"},
      {"arg_name", format_varchar (255)},
      {"data_type", format_varchar (16)},
      {"mode", format_varchar (6)},
      {"is_optional", format_varchar (3)},
      {"default_value", format_varchar (DB_MAX_DEFAULT_EXPR_LENGTH)},
      {"comment", format_varchar (1024)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_stored_procedure_args_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_serial ()
  {
    return system_catalog_definition (
		   // name
		   CTV_SERIAL_NAME,
		   // columns
    {
      /* kept for compatibility */
      {"unique_name", format_varchar (255)},
      {"name", format_varchar (255)},
      {"owner", format_varchar (DB_MAX_USER_LENGTH)},
      {"current_val", format_numeric (DB_MAX_FIXED_NUMERIC_PRECISION, 0)},
      {"increment_val", format_numeric (DB_MAX_FIXED_NUMERIC_PRECISION, 0)},
      {"max_val", format_numeric (DB_MAX_FIXED_NUMERIC_PRECISION, 0)},
      {"min_val", format_numeric (DB_MAX_FIXED_NUMERIC_PRECISION, 0)},
      {"start_val", format_numeric (DB_MAX_FIXED_NUMERIC_PRECISION, 0)},
      {"cyclic", "integer"},
      {"started", "integer"},
      {"class_name", format_varchar (255)},
      {"attr_name", format_varchar (255)},
      {"cached_num", "integer"},
      {"comment", format_varchar (1024)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_serial_spec ()},
    },
// constraints
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_ha_apply_info ()
  {
    return system_catalog_definition (
		   // name
		   CTV_HA_APPLY_INFO_NAME,
		   // columns
    {
      {"db_name", format_varchar (255)},
      {"db_creation_time", "datetime"},
      {"copied_log_path", format_varchar (4096)},
      {"committed_lsa_pageid", "bigint"},
      {"committed_lsa_offset", "integer"},
      {"committed_rep_pageid", "bigint"},
      {"committed_rep_offset", "integer"},
      {"append_lsa_pageid", "bigint"},
      {"append_lsa_offset", "integer"},
      {"eof_lsa_pageid", "bigint"},
      {"eof_lsa_offset", "integer"},
      {"final_lsa_pageid", "bigint"},
      {"final_lsa_offset", "integer"},
      {"required_lsa_pageid", "bigint"},
      {"required_lsa_offset", "integer"},
      {"log_record_time", "datetime"},
      {"log_commit_time", "datetime"},
      {"last_access_time", "datetime"},
      {"status", "integer"},
      {"insert_counter", "bigint"},
      {"update_counter", "bigint"},
      {"delete_counter", "bigint"},
      {"schema_counter", "bigint"},
      {"commit_counter", "bigint"},
      {"fail_counter", "bigint"},
      {"start_time", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_ha_apply_info_spec ()},
    },
// constraints
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_collation ()
  {
    return system_catalog_definition (
		   // name
		   CTV_COLLATION_NAME,
		   // columns
    {
      {"coll_id", "integer"},
      {"coll_name", format_varchar (32)},
      {"charset_name", format_varchar (32)},
      {"is_builtin", format_varchar (3)},
      {"has_expansions", format_varchar (3)},
      {"contractions", "integer"},
      {"uca_strength", format_varchar (255)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_collation_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_user ()
  {
    return system_catalog_definition (
		   // name
		   CTV_USER_NAME,
		   // columns
    {
      {"name", format_varchar (DB_MAX_USER_LENGTH)},
      {"id", "integer"},
      /* kept for compatibility; always NULL in view */
      {"password", AU_PASSWORD_CLASS_NAME},
      {"direct_groups", format_set (format_varchar (DB_MAX_USER_LENGTH))},
      {"groups", format_set (format_varchar (DB_MAX_USER_LENGTH))},
      /* kept for compatibility; always NULL in view */
      {"authorization", AU_AUTH_CLASS_NAME},
      {"triggers", format_sequence ("object")},
      {"is_loginable", format_varchar (3)},
      {"is_system_created", format_varchar (3)},
      {"comment", format_varchar (1024)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      // class methods (only find_user and login are exposed in view)
      {attribute_kind::CLASS_METHOD, "find_user", "au_find_user_method"},
      {attribute_kind::CLASS_METHOD, "login", "au_login_method"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_user_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, (DB_AUTH) (AU_SELECT | AU_EXECUTE), false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_authorization ()
  {
    return system_catalog_definition (
		   // name
		   CTV_AUTHORIZATION_NAME,
		   // columns
    {
      /* "owner_name" by convention, but "owner" for compatibility */
      {"owner", format_varchar (DB_MAX_USER_LENGTH)},
      {"grants", "sequence of"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_authorization_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_charset ()
  {
    return system_catalog_definition (
		   // name
		   CTV_CHARSET_NAME,
		   // columns
    {
      {CT_DBCHARSET_CHARSET_ID, "integer"},
      {CT_DBCHARSET_CHARSET_NAME, format_varchar (32)},
      {CT_DBCHARSET_DEFAULT_COLLATION, format_varchar (32)},
      {CT_DBCHARSET_CHAR_SIZE, "int"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_charset_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_synonym ()
  {
    return system_catalog_definition (
		   // name
		   CTV_SYNONYM_NAME,
		   // columns
    {
      {"synonym_name", format_varchar (255)},
      {"synonym_owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"is_public_synonym", format_varchar (3)},	/* access_modifier */
      {"target_name", format_varchar (255)},
      {"target_owner_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"comment", format_varchar (2048)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_synonym_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );
  }

  system_catalog_definition
  system_catalog_initializer::get_view_server ()
  {
    return system_catalog_definition (
		   // name
		   CTV_SERVER_NAME,
		   // columns
    {
      {"link_name", format_varchar (255)},
      {"host", format_varchar (255)},
      {"port", "integer"},
      {"db_name", format_varchar (255)},
      /* dblink remote user_name; kept 255 for external DBMS compatibility */
      {"user_name", format_varchar (255)},
      // {"password", format_varchar(256)}
      {"properties", format_varchar (2048)},
      {"owner", format_varchar (DB_MAX_USER_LENGTH)},
      {"comment", format_varchar (1024)},
      {"created_time", "datetime"},
      {"updated_time", "datetime"},
      {"invalidated_time", "datetime"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_server_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );

  }

  system_catalog_definition
  system_catalog_initializer::get_view_db_histogram ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CTV_HISTOGRAM_NAME,
		   // columns
    {
      {"class_name", "object"},
      {"key_attr", format_varchar (255)},
      {"with_fullscan", format_varchar (32)},
      {"null_frequency", "double"},
      {attribute_kind::QUERY_SPEC, sm_define_view_histogram_spec ()}
    },
// constraint
    {},
// authorization
    {
      // owner
      Au_dba_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    nullptr
	   );

  }
}
