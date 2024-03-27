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
#include "cnv.h"
#include "db.h"
#include "dbtype_function.h"
#include "schema_system_catalog_constants.h"
#include "work_space.h"
#include "schema_manager.h"
#include "schema_system_catalog_builder.hpp"
#include "schema_system_catalog_definition.hpp"
#include "authenticate.h"
#include "locator_cl.h"

using namespace std::literals;

/* ========================================================================== */
/* NEW DEFINITION (initializers for CLASS) */
/* ========================================================================== */
int
catcls_add_data_type (struct db_object *class_mop)
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
    "NCHAR", "VARNCHAR", NULL /* RESULTSET */, NULL /* MIDXKEY */,
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
catcls_add_collations (struct db_object *class_mop)
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

int
catcls_add_charsets (struct db_object *class_mop)
{
  int i;
  int count_collations;

  count_collations = lang_collation_count ();

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
  ADD_TABLE_DEFINITION (CT_METHSIG_NAME, system_catalog_initializer::get_method_sig ());
  ADD_TABLE_DEFINITION (CT_METHARG_NAME, system_catalog_initializer::get_meth_argument ());
  ADD_TABLE_DEFINITION (CT_METHFILE_NAME, system_catalog_initializer::get_meth_file ());
  ADD_TABLE_DEFINITION (CT_QUERYSPEC_NAME, system_catalog_initializer::get_query_spec ());
  ADD_TABLE_DEFINITION (CT_INDEX_NAME, system_catalog_initializer::get_index ());
  ADD_TABLE_DEFINITION (CT_INDEXKEY_NAME, system_catalog_initializer::get_index_key ());
  ADD_TABLE_DEFINITION (CT_CLASSAUTH_NAME, system_catalog_initializer::get_class_authorization ());
  ADD_TABLE_DEFINITION (CT_PARTITION_NAME, system_catalog_initializer::get_partition());
  ADD_TABLE_DEFINITION (CT_DATATYPE_NAME, system_catalog_initializer::get_data_type());
  ADD_TABLE_DEFINITION (CT_STORED_PROC_NAME, system_catalog_initializer::get_stored_procedure());
  ADD_TABLE_DEFINITION (CT_STORED_PROC_ARGS_NAME, system_catalog_initializer::get_stored_procedure_arguments());
  ADD_TABLE_DEFINITION (CT_SERIAL_NAME, system_catalog_initializer::get_serial());
  ADD_TABLE_DEFINITION (CT_HA_APPLY_INFO_NAME, system_catalog_initializer::get_ha_apply_info());
  ADD_TABLE_DEFINITION (CT_COLLATION_NAME, system_catalog_initializer::get_collations());
  ADD_TABLE_DEFINITION (CT_CHARSET_NAME, system_catalog_initializer::get_charsets());
  ADD_TABLE_DEFINITION (CT_DUAL_NAME, system_catalog_initializer::get_dual());
  ADD_TABLE_DEFINITION (CT_SYNONYM_NAME, system_catalog_initializer::get_synonym());
  ADD_TABLE_DEFINITION (CT_DB_SERVER_NAME, system_catalog_initializer::get_db_server());

  ADD_VIEW_DEFINITION (CTV_CLASS_NAME, system_catalog_initializer::get_view_class ());
  ADD_VIEW_DEFINITION (CTV_SUPER_CLASS_NAME, system_catalog_initializer::get_view_super_class ());
  ADD_VIEW_DEFINITION (CTV_VCLASS_NAME, system_catalog_initializer::get_view_vclass ());
  ADD_VIEW_DEFINITION (CTV_ATTRIBUTE_NAME, system_catalog_initializer::get_view_attribute ());
  ADD_VIEW_DEFINITION (CTV_ATTR_SD_NAME, system_catalog_initializer::get_view_attribute_set_domain ());
  ADD_VIEW_DEFINITION (CTV_METHOD_NAME, system_catalog_initializer::get_view_method ());
  ADD_VIEW_DEFINITION (CTV_METHARG_NAME, system_catalog_initializer::get_view_method_argument ());
  ADD_VIEW_DEFINITION (CTV_METHARG_SD_NAME, system_catalog_initializer::get_view_method_argument_set_domain ());
  ADD_VIEW_DEFINITION (CTV_METHFILE_NAME, system_catalog_initializer::get_view_method_file ());
  ADD_VIEW_DEFINITION (CTV_INDEX_NAME, system_catalog_initializer::get_view_index ());
  ADD_VIEW_DEFINITION (CTV_INDEXKEY_NAME, system_catalog_initializer::get_view_index_key ());
  ADD_VIEW_DEFINITION (CTV_AUTH_NAME, system_catalog_initializer::get_view_authorization ());
  ADD_VIEW_DEFINITION (CTV_TRIGGER_NAME, system_catalog_initializer::get_view_trigger ());
  ADD_VIEW_DEFINITION (CTV_PARTITION_NAME, system_catalog_initializer::get_view_partition ());
  ADD_VIEW_DEFINITION (CTV_STORED_PROC_NAME, system_catalog_initializer::get_view_stored_procedure ());
  ADD_VIEW_DEFINITION (CTV_STORED_PROC_ARGS_NAME, system_catalog_initializer::get_view_stored_procedure_arguments ());
  ADD_VIEW_DEFINITION (CTV_DB_COLLATION_NAME, system_catalog_initializer::get_view_db_collation ());
  ADD_VIEW_DEFINITION (CTV_DB_CHARSET_NAME, system_catalog_initializer::get_view_db_charset ());
  ADD_VIEW_DEFINITION (CTV_DB_SERVER_NAME, system_catalog_initializer::get_view_db_server ());
  ADD_VIEW_DEFINITION (CTV_SYNONYM_NAME, system_catalog_initializer::get_view_synonym ());
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
	  error_code = er_errid ();
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

      if (er_errid () != NO_ERROR)
	{
	  error_code = er_errid ();
	}


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


// TODO: find right place
// TODO: implement formatting utility function for std::string (like fmt library)
  const inline std::string format_varchar (const int size)
  {
    std::string s ("varchar(");
    s += std::to_string (size);
    s += ")";
    return s;
  }

  const inline std::string format_numeric (const int prec, const int scale)
  {
    std::string s ("numeric(");
    s += std::to_string (prec);
    s += ",";
    s += std::to_string (scale);
    s += ")";
    return s;
  }

  const inline std::string format_sequence (const std::string_view type)
  {
    std::string s ("sequence of");
    if (!type.empty ())
      {
	s.append (" ");
	s.append (type);
      }
    return s;
  }

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
      {"partition", format_sequence (CT_PARTITION_NAME)}
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
      {DB_CONSTRAINT_INDEX, "", {"class_name", "owner", nullptr}, false}
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {}
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
      {"default_value", format_varchar (255)},
      {"domains", format_sequence (CT_DOMAIN_NAME)},
      {"is_nullable", "integer"},
      {"comment", format_varchar (2048)}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"class_of", "attr_name", "attr_type", nullptr}, false}
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {}
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
      Au_dba_user, {}
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
      Au_dba_user, {}
    },
// initializer
    nullptr
	   );


  }

  system_catalog_definition
  system_catalog_initializer::get_method_sig ()
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
      Au_dba_user, {}
    },
// initializer
    nullptr
	   );


  }

  system_catalog_definition
  system_catalog_initializer::get_meth_argument ()
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
      Au_dba_user, {}
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
      Au_dba_user, {}
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
      {"spec_org", format_varchar (1073741823)}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"class_of", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {}
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
		   CT_QUERYSPEC_NAME,
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
      {"comment", format_varchar (1024)},
      {"status", "integer"}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"class_of", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {}
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
		   CT_QUERYSPEC_NAME,
		   // columns
    {
      {"index_of", CT_INDEX_NAME},
      {"key_attr_name", format_varchar (255)},
      {"key_order", "integer"},
      {"asc_desc", "integer"},
      {
	"key_prefix_length", "integer", [] (DB_VALUE* val)
	{
	  return db_make_int (val, -1);
	}
      },
      {"func", format_varchar (1023)}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"index_of", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {}
    },
// initializer
    nullptr
	   );


  }

  system_catalog_definition
  system_catalog_initializer::get_class_authorization ()
  {

    return system_catalog_definition (
		   // name
		   CT_CLASSAUTH_NAME,
		   // columns
    {
      {"grantor", AU_USER_CLASS_NAME},
      {"grantee", AU_USER_CLASS_NAME},
      {"class_of", CT_CLASS_NAME},
      {"auth_type", format_varchar (7)},
      {"is_grantable", "integer"}
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"grantee", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {}
    },
// initializer
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
      {"comment", format_varchar (1024)},
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"class_of", "pname", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {}
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
		   CT_PARTITION_NAME,
		   // columns
    {
      {"type_id", "integer"},
      {"type_name", format_varchar (16)}
    },
// constraints
    {},
// authorization
    {
      // owner, grants
      Au_dba_user, {}
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
      {"sp_name", format_varchar (255)},
      {"sp_type", "integer"},
      {"return_type", "integer"},
      {"arg_count", "integer"},
      {"args", format_sequence (CT_STORED_PROC_ARGS_NAME)},
      {"lang", "integer"},
      {"target", format_varchar (4096)},
      {"owner", AU_USER_CLASS_NAME},
      {"comment", format_varchar (1024)}
    },
// constraints
    {
      {DB_CONSTRAINT_UNIQUE, "", {"sp_name", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {}
    },
// initializer
    nullptr
	   );


  }

  system_catalog_definition
  system_catalog_initializer::get_stored_procedure_arguments ()
  {

    return system_catalog_definition (
		   // name
		   CT_STORED_PROC_ARGS_NAME,
		   // columns
    {
      {"sp_name", format_varchar (255)},
      {"index_of", "integer"},
      {"arg_name", format_varchar (255)},
      {"data_type", "integer"},
      {"mode", "integer"},
      {"comment", format_varchar (1024)},
    },
// constraints
    {
      {DB_CONSTRAINT_INDEX, "", {"sp_name", nullptr}, false},
    },
// authorization
    {
      // owner, grants
      Au_dba_user, {}
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
      {
	"current_val", format_numeric (DB_MAX_NUMERIC_PRECISION, 0), [] (DB_VALUE* val)
	{
	  return db_make_numeric (val, (DB_C_NUMERIC) "1", DB_MAX_NUMERIC_PRECISION, 0);
	}
      },
      {
	"increment_val", format_numeric (DB_MAX_NUMERIC_PRECISION, 0), [] (DB_VALUE* val)
	{
	  return db_make_numeric (val, (DB_C_NUMERIC) "1", DB_MAX_NUMERIC_PRECISION, 0);
	}
      },
      {"max_val", format_numeric (DB_MAX_NUMERIC_PRECISION, 0)},
      {"min_val", format_numeric (DB_MAX_NUMERIC_PRECISION, 0)},
      {
	"cyclic", "integer", [] (DB_VALUE* val)
	{
	  return db_make_int (val, 0);
	}
      },
      {
	"started", "integer", [] (DB_VALUE* val)
	{
	  return db_make_int (val, 0);
	}
      },
      {"class_name", "string"},
      {"att_name", "string"},
      {attribute_kind::CLASS_METHOD, "change_serial_owner", "au_change_serial_owner_method"},
      {
	"cached_num", "integer", [] (DB_VALUE* val)
	{
	  return db_make_int (val, 0);
	}
      },
      {"comment", format_varchar (1024)},
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
  system_catalog_initializer::get_collations ()
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
    {},
// authorization
    {
      // owner, grants
      Au_dba_user, {}
    },
// initializer
    catcls_add_collations
	   );


  }

  system_catalog_definition
  system_catalog_initializer::get_charsets ()
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
      Au_dba_user, {}
    },
// initializer
    catcls_add_charsets
	   );


  }

  system_catalog_definition
  system_catalog_initializer::get_dual ()
  {
#define CT_DUAL_DUMMY   "dummy"
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
	{Au_public_user, AU_SELECT, false}
      }
    },
// initializer
    [] (MOP class_mop)
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
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      error_code = db_put_internal (obj, CT_DUAL_DUMMY, &val);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
      return error_code;
    }
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
      {
	"is_public", "integer", [] (DB_VALUE* val)
	{
	  return db_make_int (val, 0);
	}
      },
      {"target_unique_name", format_varchar (255)},
      {"target_name", format_varchar (255)},
      {"target_owner", AU_USER_CLASS_NAME},
      {"comment", format_varchar (2048)},
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
      Au_dba_user, {}
    },
// initializer
    nullptr
	   );


  }

  system_catalog_definition
  system_catalog_initializer::get_db_server ()
  {

    return system_catalog_definition (
		   // name
		   CT_DB_SERVER_NAME,
		   // columns
    {
      {"link_name", format_varchar (255)},
      {"host", format_varchar (255)},
      {"port", "integer"},
      {"db_name", format_varchar (255)},
      {"user_name", format_varchar (255)},
      {"password", "string"},
      {"properties", format_varchar (2048)},
      {"owner", AU_USER_CLASS_NAME},
      {"comment", format_varchar (1024)}
    },
// constraints
    {
      {DB_CONSTRAINT_PRIMARY_KEY, "", {"link_name", "owner", nullptr}, false}
    },
// authorization
    {
      // owner, grants
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
// db_class
    return system_catalog_definition (
		   // name
		   CTV_CLASS_NAME,
		   // columns
    {
      {"class_name", format_varchar (255)},
      {"owner_name", format_varchar (255)},
      {"class_type", "varchar(6)"},
      {"is_system_class", "varchar(3)"},
      {"tde_algorithm", "varchar(32)"},
      {"partitioned", "varchar(3)"},
      {"is_reuse_oid_class", "varchar(3)"},
      {"collation", "varchar(32)"},
      {"comment", format_varchar (2048)},
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
  system_catalog_initializer::get_view_super_class ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CTV_SUPER_CLASS_NAME,
		   // columns
    {
      {"class_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"super_class_name", "varchar(255)"},
      {"super_owner_name", "varchar(255)"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_super_class_spec ()}
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
// db_class
    return system_catalog_definition (
		   // name
		   CTV_VCLASS_NAME,
		   // columns
    {
      {"vclass_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"vclass_def", "varchar(1073741823)"},
      {"vclass_org", "varchar(1073741823)"},
      {"comment", "varchar(2048)"},
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
// db_class
    return system_catalog_definition (
		   // name
		   CTV_ATTRIBUTE_NAME,
		   // columns
    {
      {"attr_name", "varchar(255)"},
      {"class_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"attr_type", "varchar(8)"},
      {"def_order", "integer"},
      {"from_class_name", "varchar(255)"},
      {"from_owner_name", "varchar(255)"},
      {"from_attr_name", "varchar(255)"},
      {"data_type", "varchar(9)"},
      {"prec", "integer"},
      {"scale", "integer"},
      {"charset", "varchar(32)"},
      {"collation", "varchar(32)"},
      {"domain_class_name", "varchar(255)"},
      {"domain_owner_name", "varchar(255)"},
      {"default_value", "varchar(255)"},
      {"is_nullable", "varchar(3)"},
      {"comment", "varchar(1024)"},
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
  system_catalog_initializer::get_view_attribute_set_domain ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CTV_ATTR_SD_NAME,
		   // columns
    {
      {"attr_name", "varchar(255)"},
      {"class_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"attr_type", "varchar(8)"},
      {"data_type", "varchar(9)"},
      {"prec", "integer"},
      {"scale", "integer"},
      {"code_set", "integer"},
      {"domain_class_name", "varchar(255)"},
      {"domain_owner_name", "varchar(255)"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_attribute_set_domain_spec ()}
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
// db_class
    return system_catalog_definition (
		   // name
		   CTV_METHOD_NAME,
		   // columns
    {
      {"meth_name", "varchar(255)"},
      {"class_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"meth_type", "varchar(8)"},
      {"from_class_name", "varchar(255)"},
      {"from_owner_name", "varchar(255)"},
      {"from_meth_name", "varchar(255)"},
      {"func_name", "varchar(255)"},
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
  system_catalog_initializer::get_view_method_argument ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CTV_METHARG_NAME,
		   // columns
    {
      {"meth_name", "varchar(255)"},
      {"class_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"meth_type", "varchar(8)"},
      {"index_of", "integer"},
      {"data_type", "varchar(9)"},
      {"prec", "integer"},
      {"scale", "integer"},
      {"code_set", "integer"},
      {"domain_class_name", "varchar(255)"},
      {"domain_owner_name", "varchar(255)"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_method_argument_spec ()}
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
  system_catalog_initializer::get_view_method_argument_set_domain ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CTV_METHARG_SD_NAME,
		   // columns
    {
      {"meth_name", "varchar(255)"},
      {"class_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"meth_type", "varchar(8)"},
      {"index_of", "integer"},
      {"data_type", "varchar(9)"},
      {"prec", "integer"},
      {"scale", "integer"},
      {"code_set", "integer"},
      {"domain_class_name", "varchar(255)"},
      {"domain_owner_name", "varchar(255)"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_method_argument_set_domain_spec ()}
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
  system_catalog_initializer::get_view_method_file ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CTV_METHFILE_NAME,
		   // columns
    {
      {"class_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"path_name", "varchar(255)"},
      {"from_class_name", "varchar(255)"},
      {"from_owner_name", "varchar(255)"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_method_file_spec ()}
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
// db_class
    return system_catalog_definition (
		   // name
		   CTV_INDEX_NAME,
		   // columns
    {
      {"index_name", "varchar(255)"},
      {"is_unique", "varchar(3)"},
      {"is_reverse", "varchar(3)"},
      {"class_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"key_count", "integer"},
      {"is_primary_key", "varchar(3)"},
      {"is_foreign_key", "varchar(3)"},
#if 0				// Not yet, Disabled for QA verification convenience
      {"is_deduplicate", "varchar(3)"},
      {"deduplicate_key_level", "smallint"},
#endif
      {"filter_expression", "varchar(1073741823)"},
      {"have_function", "varchar(3)"},
      {"comment", "varchar(1024)"},
      {"status", "varchar(255)"},
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
// db_class
    return system_catalog_definition (
		   // name
		   CTV_INDEXKEY_NAME,
		   // columns
    {
      {"index_name", "varchar(255)"},
      {"class_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"key_attr_name", "varchar(255)"},
      {"key_order", "integer"},
      {"asc_desc", "varchar(4)"},
      {"key_prefix_length", "integer"},
      {"func", "varchar(1023)"},
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
  system_catalog_initializer::get_view_authorization ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CTV_AUTH_NAME,
		   // columns
    {
      {"grantor_name", "varchar(255)"},
      {"grantee_name", "varchar(255)"},
      {"class_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"auth_type", "varchar(7)"},
      {"is_grantable", "varchar(3)"},
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
  system_catalog_initializer::get_view_trigger ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CTV_TRIGGER_NAME,
		   // columns
    {
      {"trigger_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"target_class_name", "varchar(255)"},
      {"target_owner_name", "varchar(255)"},
      {"target_attr_name", "varchar(255)"},
      {"target_attr_type", "varchar(8)"},
      {"action_type", "integer"},
      {"action_time", "integer"},
      {"comment", "varchar(1024)"},
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
// db_class
    return system_catalog_definition (
		   // name
		   CTV_PARTITION_NAME,
		   // columns
    {
      {"class_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"partition_name", "varchar(255)"},
      {"partition_class_name", "varchar(255)"},
      {"partition_type", "varchar(32)"},
      {"partition_expr", "varchar(2048)"},
      {"partition_values", "sequence of"},
      {"comment", "varchar(1024)"},
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
// db_class
    return system_catalog_definition (
		   // name
		   CTV_STORED_PROC_NAME,
		   // columns
    {
      {"sp_name", "varchar(255)"},
      {"sp_type", "varchar(16)"},
      {"return_type", "varchar(16)"},
      {"arg_count", "integer"},
      {"lang", "varchar(16)"},
      {"target", "varchar(4096)"},
      {"owner", "varchar(256)"},
      {"comment", "varchar(1024)"},
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
  system_catalog_initializer::get_view_stored_procedure_arguments ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CTV_STORED_PROC_ARGS_NAME,
		   // columns
    {
      {"sp_name", "varchar(255)"},
      {"index_of", "integer"},
      {"arg_name", "varchar(255)"},
      {"data_type", "varchar(16)"},
      {"mode", "varchar(6)"},
      {"comment", "varchar(1024)"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_stored_procedure_arguments_spec ()}
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
  system_catalog_initializer::get_view_db_collation ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CTV_DB_COLLATION_NAME,
		   // columns
    {
      {"coll_id", "integer"},
      {"coll_name", "varchar(32)"},
      {"charset_name", "varchar(32)"},
      {"is_builtin", "varchar(3)"},
      {"has_expansions", "varchar(3)"},
      {"contractions", "integer"},
      {"uca_strength", "varchar(255)"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_db_collation_spec ()}
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
  system_catalog_initializer::get_view_db_charset ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CTV_DB_CHARSET_NAME,
		   // columns
    {
      {CT_DBCHARSET_CHARSET_ID, "integer"},
      {CT_DBCHARSET_CHARSET_NAME, "varchar(32)"},
      {CT_DBCHARSET_DEFAULT_COLLATION, "varchar(32)"},
      {CT_DBCHARSET_CHAR_SIZE, "int"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_db_charset_spec ()}
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
// db_class
    return system_catalog_definition (
		   // name
		   CTV_SYNONYM_NAME,
		   // columns
    {
      {"synonym_name", "varchar(255)"},
      {"synonym_owner_name", "varchar(255)"},
      {"is_public_synonym", "varchar(3)"},	/* access_modifier */
      {"target_name", "varchar(255)"},
      {"target_owner_name", "varchar(255)"},
      {"comment", "varchar(2048)"},
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
  system_catalog_initializer::get_view_db_server ()
  {
// db_class
    return system_catalog_definition (
		   // name
		   CTV_DB_SERVER_NAME,
		   // columns
    {
      {"link_name", "varchar(255)"},
      {"host", "varchar(255)"},
      {"port", "integer"},
      {"db_name", "varchar(255)"},
      {"user_name", "varchar(255)"},
      // {"password", "varchar(256)"}
      {"properties", "varchar(2048)"},
      {"owner", "varchar(255)"},
      {"comment", "varchar(1024)"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_db_server_spec ()}
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
