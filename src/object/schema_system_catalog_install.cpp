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

#include "schema_system_catalog.hpp"

#include "cnv.h"
#include "db.h"
#include "dbtype_function.h"
#include "transform.h"
#include "work_space.h"
#include "schema_manager.h"
#include "schema_system_catalog_builder.hpp"
#include "schema_system_catalog_definition.hpp"
#include "authenticate.h"
#include "locator_cl.h"

using namespace std::literals;

// query specs
static const char *sm_define_view_class_spec (void);
static const char *sm_define_view_super_class_spec (void);
static const char *sm_define_view_vclass_spec (void);
static const char *sm_define_view_attribute_spec (void);
static const char *sm_define_view_attribute_set_domain_spec (void);
static const char *sm_define_view_method_spec (void);
static const char *sm_define_view_method_argument_spec (void);
static const char *sm_define_view_method_argument_set_domain_spec (void);
static const char *sm_define_view_method_file_spec (void);
static const char *sm_define_view_index_spec (void);
static const char *sm_define_view_index_key_spec (void);
static const char *sm_define_view_authorization_spec (void);
static const char *sm_define_view_trigger_spec (void);
static const char *sm_define_view_partition_spec (void);
static const char *sm_define_view_stored_procedure_spec (void);
static const char *sm_define_view_stored_procedure_arguments_spec (void);
static const char *sm_define_view_db_collation_spec (void);
static const char *sm_define_view_db_charset_spec (void);
static const char *sm_define_view_synonym_spec (void);
static const char *sm_define_view_db_server_spec (void);


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

  const std::string OBJECT {"object"};
  const std::string INTEGER {"integer"};
  const std::string VARCHAR_255 {format_varchar (255)};
  const std::string VARCHAR_1024 {format_varchar (1024)};
  const std::string VARCHAR_2048 {format_varchar (2048)}; // for comment
  const std::string STRING {format_varchar (1073741823)};

  class system_catalog_initializer
  {
    public:
      // classes
      static const system_catalog_definition &get_class ();
      static const system_catalog_definition &get_attribute ();
      static const system_catalog_definition &get_domain ();
      static const system_catalog_definition &get_method ();
      static const system_catalog_definition &get_method_sig ();
      static const system_catalog_definition &get_meth_argument ();
      static const system_catalog_definition &get_meth_file ();
      static const system_catalog_definition &get_query_spec ();
      static const system_catalog_definition &get_index ();
      static const system_catalog_definition &get_index_key ();
      static const system_catalog_definition &get_class_authorization ();
      static const system_catalog_definition &get_partition ();
      static const system_catalog_definition &get_data_type ();
      static const system_catalog_definition &get_stored_procedure ();
      static const system_catalog_definition &get_stored_procedure_arguments ();
      static const system_catalog_definition &get_serial ();

      static const system_catalog_definition &get_ha_apply_info ();
      static const system_catalog_definition &get_collations ();
      static const system_catalog_definition &get_charsets ();
      static const system_catalog_definition &get_dual ();
      static const system_catalog_definition &get_db_server ();
      static const system_catalog_definition &get_synonym ();

      // views
      static const system_catalog_definition &get_view_class ();
      static const system_catalog_definition &get_view_super_class ();
      static const system_catalog_definition &get_view_vclass ();
      static const system_catalog_definition &get_view_attribute ();
      static const system_catalog_definition &get_view_attribute_set_domain ();
      static const system_catalog_definition &get_view_method ();
      static const system_catalog_definition &get_view_method_argument ();
      static const system_catalog_definition &get_view_method_argument_set_domain ();
      static const system_catalog_definition &get_view_method_file ();
      static const system_catalog_definition &get_view_index ();
      static const system_catalog_definition &get_view_index_key ();
      static const system_catalog_definition &get_view_authorization ();
      static const system_catalog_definition &get_view_trigger ();
      static const system_catalog_definition &get_view_partition ();
      static const system_catalog_definition &get_view_stored_procedure ();
      static const system_catalog_definition &get_view_stored_procedure_arguments ();
      static const system_catalog_definition &get_view_db_collation ();
      static const system_catalog_definition &get_view_db_charset ();
      static const system_catalog_definition &get_view_synonym ();
      static const system_catalog_definition &get_view_db_server ();
  };

  const system_catalog_definition &
  system_catalog_initializer::get_class ()
  {
    static system_catalog_definition sm_define_class (
	    // name
	    CT_CLASS_NAME,
	    // columns
    {
      {"class_of", OBJECT},
      {"unique_name", VARCHAR_255},
      {"class_name", VARCHAR_255},
      {"class_type", INTEGER},
      {"is_system_class", INTEGER},
      {"owner", AU_USER_CLASS_NAME},
      {"inst_attr_count", INTEGER},
      {"class_attr_count", INTEGER},
      {"shared_attr_count", INTEGER},
      {"inst_meth_count", INTEGER},
      {"class_meth_count", INTEGER},
      {"collation_id", INTEGER},
      {"tde_algorithm", INTEGER},
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
      {"comment", VARCHAR_2048},
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

    return sm_define_class;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_attribute ()
  {

    static system_catalog_definition sm_define_attribute (
	    // name
	    CT_ATTRIBUTE_NAME,
	    // columns
    {
      {"class_of", CT_CLASS_NAME},
      {"attr_name", VARCHAR_255},
      {"attr_type", INTEGER},
      {"from_class_of", CT_CLASS_NAME},
      {"from_attr_name", VARCHAR_255},
      {"def_order", INTEGER},
      {"data_type", INTEGER},
      {"default_value", VARCHAR_255},
      {"domains", format_sequence (CT_DOMAIN_NAME)},
      {"is_nullable", INTEGER},
      {"comment", VARCHAR_2048}
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

    return sm_define_attribute;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_domain ()
  {

    static system_catalog_definition sm_define_domain (
	    // name
	    CT_DOMAIN_NAME,
	    // columns
    {
      {"object_of", OBJECT},
      {"data_type", INTEGER},
      {"prec", INTEGER},
      {"scale", INTEGER},
      {"class_of", CT_CLASS_NAME},
      {"code_set", INTEGER},
      {"collation_id", INTEGER},
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

    return sm_define_domain;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_method ()
  {

    static system_catalog_definition sm_define_method (
	    // name
	    CT_METHOD_NAME,
	    // columns
    {
      {"class_of", CT_CLASS_NAME},
      {"meth_name", VARCHAR_255},
      {"meth_type", INTEGER},
      {"from_class_of", CT_CLASS_NAME},
      {"from_meth_name", VARCHAR_255},
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

    return sm_define_method;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_method_sig ()
  {

    static system_catalog_definition sm_define_method_sig (
	    // name
	    CT_METHSIG_NAME,
	    // columns
    {
      {"meth_of", CT_METHOD_NAME},
      {"func_name", VARCHAR_255},
      {"arg_count", INTEGER},
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

    return sm_define_method_sig;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_meth_argument ()
  {

    static system_catalog_definition sm_define_meth_argument (
	    // name
	    CT_METHARG_NAME,
	    // columns
    {
      {"meth_sig_of", CT_METHSIG_NAME},
      {"data_type", INTEGER},
      {"index_of", INTEGER},
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

    return sm_define_meth_argument;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_meth_file ()
  {

    static system_catalog_definition sm_define_meth_file (
	    // name
	    CT_METHFILE_NAME,
	    // columns
    {
      {"class_of", CT_CLASS_NAME},
      {"from_class_of", CT_CLASS_NAME},
      {"path_name", VARCHAR_255}
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

    return sm_define_meth_file;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_query_spec ()
  {

    static system_catalog_definition sm_define_query_spec (
	    // name
	    CT_QUERYSPEC_NAME,
	    // columns
    {
      {"class_of", CT_CLASS_NAME},
      {"spec", STRING}
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

    return sm_define_query_spec;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_index ()
  {

    static system_catalog_definition sm_define_index (
	    // name
	    CT_QUERYSPEC_NAME,
	    // columns
    {
      {"class_of", CT_CLASS_NAME},
      {"index_name", VARCHAR_255},
      {"is_unique", INTEGER},
      {"key_count", INTEGER},
      {"key_attrs", format_sequence (CT_INDEXKEY_NAME)},
      {"is_reverse", INTEGER},
      {"is_primary_key", INTEGER},
      {"is_foreign_key", INTEGER},
      {"filter_expression", VARCHAR_255},
      {"have_function", INTEGER},
      {"comment", VARCHAR_1024},
      {"status", INTEGER}
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

    return sm_define_index;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_index_key ()
  {

    static system_catalog_definition sm_define_index_key (
	    // name
	    CT_QUERYSPEC_NAME,
	    // columns
    {
      {"index_of", CT_INDEX_NAME},
      {"key_attr_name", VARCHAR_255},
      {"key_order", INTEGER},
      {"asc_desc", INTEGER},
      {
	"key_prefix_length", INTEGER, [] (DB_VALUE* val)
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

    return sm_define_index_key;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_class_authorization ()
  {

    static system_catalog_definition sm_define_class_authorization (
	    // name
	    CT_CLASSAUTH_NAME,
	    // columns
    {
      {"grantor", AU_USER_CLASS_NAME},
      {"grantee", AU_USER_CLASS_NAME},
      {"class_of", CT_CLASS_NAME},
      {"auth_type", format_varchar (7)},
      {"is_grantable", INTEGER}
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

    return sm_define_class_authorization;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_partition ()
  {

    static system_catalog_definition sm_define_partition (
	    // name
	    CT_PARTITION_NAME,
	    // columns
    {
      {"class_of", CT_CLASS_NAME},
      {"pname", VARCHAR_255},
      {"ptype", INTEGER},
      {"pexpr", format_varchar (2048)},
      {"pvalues", format_sequence ("")},
      {"comment", VARCHAR_1024},
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

    return sm_define_partition;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_data_type ()
  {

    static system_catalog_definition sm_define_data_type (
	    // name
	    CT_PARTITION_NAME,
	    // columns
    {
      {"type_id", INTEGER},
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

    return sm_define_data_type;
  }


  const system_catalog_definition &
  system_catalog_initializer::get_stored_procedure ()
  {

    static system_catalog_definition sm_define_stored_procedure (
	    // name
	    CT_STORED_PROC_NAME,
	    // columns
    {
      {"sp_name", VARCHAR_255},
      {"sp_type", INTEGER},
      {"return_type", INTEGER},
      {"arg_count", INTEGER},
      {"args", format_sequence (CT_STORED_PROC_ARGS_NAME)},
      {"lang", INTEGER},
      {"target", format_varchar (4096)},
      {"owner", AU_USER_CLASS_NAME},
      {"comment", VARCHAR_1024}
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

    return sm_define_stored_procedure;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_stored_procedure_arguments ()
  {

    static system_catalog_definition sm_define_stored_procedure_arguments (
	    // name
	    CT_STORED_PROC_ARGS_NAME,
	    // columns
    {
      {"sp_name", VARCHAR_255},
      {"index_of", INTEGER},
      {"arg_name", VARCHAR_255},
      {"data_type", INTEGER},
      {"mode", INTEGER},
      {"comment", VARCHAR_1024},
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

    return sm_define_stored_procedure_arguments;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_serial ()
  {
    static system_catalog_definition sm_define_serial (
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
	"cyclic", INTEGER, [] (DB_VALUE* val)
	{
	  return db_make_int (val, 0);
	}
      },
      {
	"started", INTEGER, [] (DB_VALUE* val)
	{
	  return db_make_int (val, 0);
	}
      },
      {"class_name", "string"},
      {"att_name", "string"},
      {attribute_kind::CLASS_METHOD, "change_serial_owner", "au_change_serial_owner_method"},
      {
	"cached_num", INTEGER, [] (DB_VALUE* val)
	{
	  return db_make_int (val, 0);
	}
      },
      {"comment", VARCHAR_1024},
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

    return sm_define_serial;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_ha_apply_info ()
  {

    static system_catalog_definition sm_define_ha_apply_info (
	    // name
	    CT_HA_APPLY_INFO_NAME,
	    // columns
    {
      {"db_name", VARCHAR_255},
      {"db_creation_time", "datetime"},
      {"copied_log_path", format_varchar (4096)},
      {"committed_lsa_pageid", "bigint"},
      {"committed_lsa_offset", INTEGER},
      {"committed_rep_pageid", "bigint"},
      {"committed_rep_offset", INTEGER},
      {"append_lsa_pageid", "bigint"},
      {"append_lsa_offset", INTEGER},
      {"eof_lsa_pageid", "bigint"},
      {"eof_lsa_offset", INTEGER},
      {"final_lsa_pageid", "bigint"},
      {"final_lsa_offset", INTEGER},
      {"required_lsa_pageid", "bigint"},
      {"required_lsa_offset", INTEGER},
      {"log_record_time", "datetime"},
      {"log_commit_time", "datetime"},
      {"last_access_time", "datetime"},
      {"status", INTEGER},
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

    return sm_define_ha_apply_info;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_collations ()
  {

    static system_catalog_definition sm_define_collations (
	    // name
	    CT_COLLATION_NAME,
	    // columns
    {
      {CT_DBCOLL_COLL_ID_COLUMN, INTEGER},
      {CT_DBCOLL_COLL_NAME_COLUMN, format_varchar (32)},
      {CT_DBCOLL_CHARSET_ID_COLUMN, INTEGER},
      {CT_DBCOLL_BUILT_IN_COLUMN, INTEGER},
      {CT_DBCOLL_EXPANSIONS_COLUMN, INTEGER},
      {CT_DBCOLL_CONTRACTIONS_COLUMN, INTEGER},
      {CT_DBCOLL_UCA_STRENGTH, INTEGER},
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

    return sm_define_collations;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_charsets ()
  {

    static system_catalog_definition sm_define_charsets (
	    // name
	    CT_CHARSET_NAME,
	    // columns
    {
      {CT_DBCHARSET_CHARSET_ID, INTEGER},
      {CT_DBCHARSET_CHARSET_NAME, format_varchar (32)},
      {CT_DBCHARSET_DEFAULT_COLLATION, INTEGER},
      {CT_DBCHARSET_CHAR_SIZE, INTEGER}
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

    return sm_define_charsets;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_dual ()
  {
#define CT_DUAL_DUMMY   "dummy"
    static system_catalog_definition sm_define_dual (
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

    return sm_define_dual;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_synonym ()
  {

    static system_catalog_definition sm_define_synonym (
	    // name
	    CT_SYNONYM_NAME,
	    // columns
    {
      {"unique_name", VARCHAR_255},
      {"name", VARCHAR_255},
      {"owner", AU_USER_CLASS_NAME},
      {
	"is_public", "integer", [] (DB_VALUE* val)
	{
	  return db_make_int (val, 0);
	}
      },
      {"target_unique_name", VARCHAR_255},
      {"target_name", VARCHAR_255},
      {"target_owner", AU_USER_CLASS_NAME},
      {"comment", VARCHAR_2048},
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

    return sm_define_synonym;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_db_server ()
  {

    static system_catalog_definition sm_define_db_server (
	    // name
	    CT_DB_SERVER_NAME,
	    // columns
    {
      {"link_name", VARCHAR_255},
      {"host", VARCHAR_255},
      {"port", INTEGER},
      {"db_name", VARCHAR_255},
      {"user_name", VARCHAR_255},
      {"password", "string"},
      {"properties", VARCHAR_2048},
      {"owner", AU_USER_CLASS_NAME},
      {"comment", VARCHAR_1024}
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

    return sm_define_db_server;
  }

  /* ========================================================================== */
  /* NEW DEFINITION (VCLASS) */
  /* ========================================================================== */

// TODO: Add checking the following rules in compile time (@hgryoo)
  /*
   * Please follow the rules below when writing query specifications for system virtual classes.
   *
   *  1. First indent 1 tab, then 2 spaces.
   *     - The CASE statement indents 2 spaces until the END.
   *  2. All lines do not start with a space.
   *  3. All lines end with a space. However, the following case does not end with a space.
   *     - If the current line ends with "(", it ends without a space.
   *     - If the next line starts with ")", the current line ends without a space.
   *  4. Add a space before "(" and after ")". Remove spaces after "(" and before ")".
   *  5. Add a space before "{" and after "}". Remove spaces after "{" and before "}".
   *  6. Add a space before and after "+" and "=" operators.
   *  7. Change the line.
   *     - In the SELECT, FROM, WHERE, and ORDER BY clauses, change the line.
   *     - After the SELECT, FROM, WHERE, and ORDER BY keywords, change the line.
   *     - In the AND and OR clauses, change the line.
   *     - In more than one TABLE expression, change the line.
   *  8. Do not change the line.
   *     - If the expression length is less than 120 characters, write it on one line.
   *     - In the CASE statement, write the WHEN and THEN clauses on one line.
   *     - In the FROM clause, write a TABLE expression and related tables on one line.
   *  9. In the SELECT and FROM clauses, use the AS keyword before alias.
   * 10. If the CAST function is used, write a comment about the data type being changed.
   * 11. If column are compared without changing in the CASE statement, write the column name after the CASE keyword.
   * 12. If %s (Format Specifier) is used in the FROM clause, write a comment about the value to be used.
   * 13. Because path expression cannot be used in ANSI style, write a join condition in the WHERE clause.
   *
   */

  const system_catalog_definition &
  system_catalog_initializer::get_view_class ()
  {
// db_class
    static system_catalog_definition sm_define_view_class (
	    // name
	    CTV_CLASS_NAME,
	    // columns
    {
      {"class_name", VARCHAR_255},
      {"owner_name", VARCHAR_255},
      {"class_type", "varchar(6)"},
      {"is_system_class", "varchar(3)"},
      {"tde_algorithm", "varchar(32)"},
      {"partitioned", "varchar(3)"},
      {"is_reuse_oid_class", "varchar(3)"},
      {"collation", "varchar(32)"},
      {"comment", VARCHAR_2048},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_class_spec (), ""}
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
    return sm_define_view_class;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_super_class ()
  {
// db_class
    static system_catalog_definition sm_define_super_class (
	    // name
	    CTV_SUPER_CLASS_NAME,
	    // columns
    {
      {"class_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"super_class_name", "varchar(255)"},
      {"super_owner_name", "varchar(255)"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_super_class_spec (), ""}
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
    return sm_define_super_class;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_vclass ()
  {
// db_class
    static system_catalog_definition sm_define_vclass (
	    // name
	    CTV_VCLASS_NAME,
	    // columns
    {
      {"vclass_name", "varchar(255)"},
      {"owner_name", "varchar(255)"},
      {"vclass_def", "varchar(1073741823)"},
      {"comment", "varchar(2048)"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_vclass_spec (), ""}
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
    return sm_define_vclass;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_attribute ()
  {
// db_class
    static system_catalog_definition sm_define_attribute (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_attribute_spec (), ""}
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
    return sm_define_attribute;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_attribute_set_domain ()
  {
// db_class
    static system_catalog_definition sm_define_attribute_set_domain (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_attribute_set_domain_spec (), ""}
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
    return sm_define_attribute_set_domain;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_method ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_method_spec (), ""}
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
    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_method_argument ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_method_argument_spec (), ""}
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
    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_method_argument_set_domain ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_method_argument_set_domain_spec (), ""}
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
    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_method_file ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_method_file_spec (), ""}
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
    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_index ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {"filter_expression", "varchar(255)"},
      {"have_function", "varchar(3)"},
      {"comment", "varchar(1024)"},
      {"status", "varchar(255)"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_index_spec (), ""}
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
    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_index_key ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_index_key_spec (), ""}
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
    return sm_def;
  }

  /* When a user is granted SELECT privilege,
   * that user can also view the list of privileges that other users have been granted.
   * Is this no problem? */

  const system_catalog_definition &
  system_catalog_initializer::get_view_authorization ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_authorization_spec (), ""}
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
    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_trigger ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_trigger_spec (), ""}
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
    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_partition ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_partition_spec (), ""}
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
    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_stored_procedure ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_stored_procedure_spec (), ""}
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
    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_stored_procedure_arguments ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_stored_procedure_arguments_spec (), ""}
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
    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_db_collation ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_db_collation_spec (), ""}
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

    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_db_charset ()
  {
// db_class
    static system_catalog_definition sm_def (
	    // name
	    CTV_DB_CHARSET_NAME,
	    // columns
    {
      {CT_DBCHARSET_CHARSET_ID, "integer"},
      {CT_DBCHARSET_CHARSET_NAME, "varchar(32)"},
      {CT_DBCHARSET_DEFAULT_COLLATION, "varchar(32)"},
      {CT_DBCHARSET_CHAR_SIZE, "int"},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_view_db_charset_spec (), ""}
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
    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_synonym ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_synonym_spec (), ""}
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
    return sm_def;
  }

  const system_catalog_definition &
  system_catalog_initializer::get_view_db_server ()
  {
// db_class
    static system_catalog_definition sm_def (
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
      {attribute_kind::QUERY_SPEC, sm_define_view_db_server_spec (), ""}
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
    return sm_def;
  }
}

// for backward compatibility
using COLUMN = cubschema::attribute;

/* ========================================================================== */
/* MAIN APIS */
/* ========================================================================== */

static std::vector<cubschema::catcls_function> *clist = nullptr;
static std::vector<cubschema::catcls_function> *vclist = nullptr;

void
catcls_init (void)
{
  // TODO: for late initialization (for au_init () to retrieve MOPs: Au_dba_user and Au_public_user)

  using namespace cubschema;
  static std::vector<cubschema::catcls_function> cl =
  {
    {CT_CLASS_NAME, system_catalog_initializer::get_class ()},
    {CT_ATTRIBUTE_NAME, system_catalog_initializer::get_attribute ()},
    {CT_DOMAIN_NAME, system_catalog_initializer::get_domain ()},
    {CT_METHOD_NAME, system_catalog_initializer::get_method ()},
    {CT_METHSIG_NAME, system_catalog_initializer::get_method_sig ()},
    {CT_METHARG_NAME, system_catalog_initializer::get_meth_argument ()},
    {CT_METHFILE_NAME, system_catalog_initializer::get_meth_file ()},
    {CT_QUERYSPEC_NAME, system_catalog_initializer::get_query_spec ()},
    {CT_INDEX_NAME, system_catalog_initializer::get_index ()},
    {CT_INDEXKEY_NAME, system_catalog_initializer::get_index_key ()},
    {CT_CLASSAUTH_NAME, system_catalog_initializer::get_class_authorization ()},
    {CT_PARTITION_NAME, system_catalog_initializer::get_partition()},
    {CT_DATATYPE_NAME, system_catalog_initializer::get_data_type()},
    {CT_STORED_PROC_NAME, system_catalog_initializer::get_stored_procedure()},
    {CT_STORED_PROC_ARGS_NAME, system_catalog_initializer::get_stored_procedure_arguments()},
    {CT_SERIAL_NAME, system_catalog_initializer::get_serial()},
    {CT_HA_APPLY_INFO_NAME, system_catalog_initializer::get_ha_apply_info()},
    {CT_COLLATION_NAME, system_catalog_initializer::get_collations()},
    {CT_CHARSET_NAME, system_catalog_initializer::get_charsets()},
    {CT_DUAL_NAME, system_catalog_initializer::get_dual()},
    {CT_SYNONYM_NAME, system_catalog_initializer::get_synonym()},
    {CT_DB_SERVER_NAME, system_catalog_initializer::get_db_server()}
  };

  static std::vector<cubschema::catcls_function> vcl =
  {
    {CTV_CLASS_NAME, system_catalog_initializer::get_view_class ()},
    {CTV_SUPER_CLASS_NAME, system_catalog_initializer::get_view_super_class ()},
    {CTV_VCLASS_NAME, system_catalog_initializer::get_view_vclass ()},
    {CTV_ATTRIBUTE_NAME, system_catalog_initializer::get_view_attribute ()},
    {CTV_ATTR_SD_NAME, system_catalog_initializer::get_view_attribute_set_domain ()},
    {CTV_METHOD_NAME, system_catalog_initializer::get_view_method ()},
    {CTV_METHARG_NAME, system_catalog_initializer::get_view_method_argument ()},
    {CTV_METHARG_SD_NAME, system_catalog_initializer::get_view_method_argument_set_domain ()},
    {CTV_METHFILE_NAME, system_catalog_initializer::get_view_method_file ()},
    {CTV_INDEX_NAME, system_catalog_initializer::get_view_index ()},
    {CTV_INDEXKEY_NAME, system_catalog_initializer::get_view_index_key ()},
    {CTV_AUTH_NAME, system_catalog_initializer::get_view_authorization ()},
    {CTV_TRIGGER_NAME, system_catalog_initializer::get_view_trigger ()},
    {CTV_PARTITION_NAME, system_catalog_initializer::get_view_partition ()},
    {CTV_STORED_PROC_NAME, system_catalog_initializer::get_view_stored_procedure ()},
    {CTV_STORED_PROC_ARGS_NAME, system_catalog_initializer::get_view_stored_procedure_arguments ()},
    {CTV_DB_COLLATION_NAME, system_catalog_initializer::get_view_db_collation ()},
    {CTV_DB_CHARSET_NAME, system_catalog_initializer::get_view_db_charset ()},
    {CTV_SYNONYM_NAME, system_catalog_initializer::get_view_synonym ()},
    {CTV_DB_SERVER_NAME, system_catalog_initializer::get_view_db_server ()}
  };

  clist = &cl;
  vclist = &vcl;
}

int
catcls_install_class (void)
{
  int error_code = NO_ERROR;

  const size_t num_classes = clist->size ();
  std::vector<MOP> class_mop (num_classes, nullptr);
  int save;
  size_t i;
  AU_DISABLE (save);

  using catalog_builder = cubschema::system_catalog_builder;

  for (i = 0; i < num_classes; i++)
    {
      // new routine
      class_mop[i] = catalog_builder::create_and_mark_system_class ((*clist)[i].name);
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
      error_code = catalog_builder::build_class (class_mop[i], (*clist)[i].definition);
      if (error_code != NO_ERROR)
	{
	  assert (false);
	  error_code = er_errid ();
	  goto end;
	}
    }

end:
  AU_ENABLE (save);

  return error_code;
}

/*
 * catcls_vclass_install :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
int
catcls_install_vclass (void)
{
  int save;
  size_t i;

  const size_t num_vclasses = vclist->size ();
  int error_code = NO_ERROR;

  AU_DISABLE (save);

  using catalog_builder = cubschema::system_catalog_builder;

  for (i = 0; i < num_vclasses; i++)
    {
      // new routine
      MOP class_mop = catalog_builder::create_and_mark_system_class ((*vclist)[i].name);
      if (class_mop != nullptr)
	{
	  error_code = catalog_builder::build_vclass (class_mop, (*vclist)[i].definition);
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

  return error_code;
}

/* ========================================================================== */
/* LEGACY FUNCTIONS (SYSTEM VCLASS) */
/* ========================================================================== */

/* ========================================================================== */
/* NEW ROUTINE (QUERY SPECS OF SYSTEM VCLASS) */
/* ========================================================================== */

/*
 * Please follow the rules below when writing query specifications for system virtual classes.
 *
 *  1. First indent 1 tab, then 2 spaces.
 *     - The CASE statement indents 2 spaces until the END.
 *  2. All lines do not start with a space.
 *  3. All lines end with a space. However, the following case does not end with a space.
 *     - If the current line ends with "(", it ends without a space.
 *     - If the next line starts with ")", the current line ends without a space.
 *  4. Add a space before "(" and after ")". Remove spaces after "(" and before ")".
 *  5. Add a space before "{" and after "}". Remove spaces after "{" and before "}".
 *  6. Add a space before and after "+" and "=" operators.
 *  7. Change the line.
 *     - In the SELECT, FROM, WHERE, and ORDER BY clauses, change the line.
 *     - After the SELECT, FROM, WHERE, and ORDER BY keywords, change the line.
 *     - In the AND and OR clauses, change the line.
 *     - In more than one TABLE expression, change the line.
 *  8. Do not change the line.
 *     - If the expression length is less than 120 characters, write it on one line.
 *     - In the CASE statement, write the WHEN and THEN clauses on one line.
 *     - In the FROM clause, write a TABLE expression and related tables on one line.
 *  9. In the SELECT and FROM clauses, use the AS keyword before alias.
 * 10. If the CAST function is used, write a comment about the data type being changed.
 * 11. If column are compared without changing in the CASE statement, write the column name after the CASE keyword.
 * 12. If %s (Format Specifier) is used in the FROM clause, write a comment about the value to be used.
 * 13. Because path expression cannot be used in ANSI style, write a join condition in the WHERE clause.
 *
 */

static const char *
sm_define_view_class_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[c].[class_name] AS [class_name], "
	  "CAST ([c].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "CASE [c].[class_type] WHEN 0 THEN 'CLASS' WHEN 1 THEN 'VCLASS' ELSE 'UNKNOW' END AS [class_type], "
	  "CASE WHEN MOD ([c].[is_system_class], 2) = 1 THEN 'YES' ELSE 'NO' END AS [is_system_class], "
	  "CASE [c].[tde_algorithm] WHEN 0 THEN 'NONE' WHEN 1 THEN 'AES' WHEN 2 THEN 'ARIA' END AS [tde_algorithm], "
	  "CASE "
	    "WHEN [c].[sub_classes] IS NULL THEN 'NO' "
	    /* CT_PARTITION_NAME */
	    "ELSE NVL ((SELECT 'YES' FROM [%s] AS [p] WHERE [p].[class_of] = [c] AND [p].[pname] IS NULL), 'NO') "
	    "END AS [partitioned], "
	  "CASE WHEN MOD ([c].[is_system_class] / 8, 2) = 1 THEN 'YES' ELSE 'NO' END AS [is_reuse_oid_class], "
	  "[coll].[coll_name] AS [collation], "
	  "[c].[comment] AS [comment] "
	"FROM "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], "
	  /* CT_COLLATION_NAME */
	  "[%s] AS [coll] "
	"WHERE "
	  "[c].[collation_id] = [coll].[coll_id] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_PARTITION_NAME,
	CT_CLASS_NAME,
	CT_COLLATION_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_super_class_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[c].[class_name] AS [class_name], "
	  "CAST ([c].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[s].[class_name] AS [super_class_name], "
	  "CAST ([s].[owner].[name] AS VARCHAR(255)) AS [super_owner_name] " /* string -> varchar(255) */
	"FROM "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], TABLE ([c].[super_classes]) AS [t] ([s]) "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[c].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[c]} SUBSETEQ ("
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",
	CT_CLASS_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_vclass_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[q].[class_of].[class_name] AS [vclass_name], "
	  "CAST ([q].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[q].[spec] AS [vclass_def], "
	  "[c].[comment] AS [comment] "
	"FROM "
	  /* CT_QUERYSPEC_NAME */
	  "[%s] AS [q], "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c] "
	"WHERE "
	  "[q].[class_of] = [c] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[q].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[q].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_QUERYSPEC_NAME,
	CT_CLASS_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_attribute_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[a].[attr_name] AS [attr_name], "
	  "[c].[class_name] AS [class_name], "
	  "CAST ([c].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "CASE [a].[attr_type] WHEN 0 THEN 'INSTANCE' WHEN 1 THEN 'CLASS' ELSE 'SHARED' END AS [attr_type], "
	  "[a].[def_order] AS [def_order], "
	  "[a].[from_class_of].[class_name] AS [from_class_name], "
	  "CAST ([a].[from_class_of].[owner].[name] AS VARCHAR(255)) AS [from_owner_name], " /* string -> varchar(255) */
	  "[a].[from_attr_name] AS [from_attr_name], "
	  "[t].[type_name] AS [data_type], "
	  "[d].[prec] AS [prec], "
	  "[d].[scale] AS [scale], "
	  "IF ("
	      "[a].[data_type] IN (4, 25, 26, 27, 35), "
	      /* CT_CHARSET_NAME */
	      "(SELECT [ch].[charset_name] FROM [%s] AS [ch] WHERE [d].[code_set] = [ch].[charset_id]), "
	      "'Not applicable'"
	    ") AS [charset], "
	  "IF ("
	      "[a].[data_type] IN (4, 25, 26, 27, 35), "
	      /* CT_COLLATION_NAME */
	      "(SELECT [coll].[coll_name] FROM [%s] AS [coll] WHERE [d].[collation_id] = [coll].[coll_id]), "
	      "'Not applicable'"
	    ") AS [collation], "
	  "[d].[class_of].[class_name] AS [domain_class_name], "
	  "CAST ([d].[class_of].[owner].[name] AS VARCHAR(255)) AS [domain_owner_name], " /* string -> varchar(255) */
	  "[a].[default_value] AS [default_value], "
	  "CASE WHEN [a].[is_nullable] = 1 THEN 'YES' ELSE 'NO' END AS [is_nullable], "
	  "[a].[comment] AS [comment] "
	"FROM "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], "
	  /* CT_ATTRIBUTE_NAME */
	  "[%s] AS [a], "
	  /* CT_DOMAIN_NAME */
	  "[%s] AS [d], "
	  /* CT_DATATYPE_NAME */
	  "[%s] AS [t] "
	"WHERE "
	  "[a].[class_of] = [c] "
	  "AND [d].[object_of] = [a] "
	  "AND [d].[data_type] = [t].[type_id] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_CHARSET_NAME,
	CT_COLLATION_NAME,
	CT_CLASS_NAME,
	CT_ATTRIBUTE_NAME,
	CT_DOMAIN_NAME,
	CT_DATATYPE_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_attribute_set_domain_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[a].[attr_name] AS [attr_name], "
	  "[c].[class_name] AS [class_name], "
	  "CAST ([c].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "CASE [a].[attr_type] WHEN 0 THEN 'INSTANCE' WHEN 1 THEN 'CLASS' ELSE 'SHARED' END AS [attr_type], "
	  "[et].[type_name] AS [data_type], "
	  "[e].[prec] AS [prec], "
	  "[e].[scale] AS [scale], "
	  "[e].[code_set] AS [code_set], "
	  "[e].[class_of].[class_name] AS [domain_class_name], "
	  "CAST ([e].[class_of].[owner].[name] AS VARCHAR(255)) AS [domain_owner_name] " /* string -> varchar(255) */
	"FROM "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], "
	  /* CT_ATTRIBUTE_NAME */
	  "[%s] AS [a], "
	  /* CT_DOMAIN_NAME */
	  "[%s] AS [d], TABLE ([d].[set_domains]) AS [t] ([e]), "
	  /* CT_DATATYPE_NAME */
	  "[%s] AS [et] "
	"WHERE "
	  "[a].[class_of] = [c] "
	  "AND [d].[object_of] = [a] "
	  "AND [e].[data_type] = [et].[type_id] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_CLASS_NAME,
	CT_ATTRIBUTE_NAME,
	CT_DOMAIN_NAME,
	CT_DATATYPE_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_method_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[m].[meth_name] AS [meth_name], "
	  "[m].[class_of].[class_name] AS [class_name], "
	  "CAST ([m].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "CASE [m].[meth_type] WHEN 0 THEN 'INSTANCE' ELSE 'CLASS' END AS [meth_type], "
	  "[m].[from_class_of].[class_name] AS [from_class_name], "
	  "CAST ([m].[from_class_of].[owner].[name] AS VARCHAR(255)) AS [from_owner_name], " /* string -> varchar(255) */
	  "[m].[from_meth_name] AS [from_meth_name], "
	  "[s].[func_name] AS [func_name] "
	"FROM "
	  /* CT_METHOD_NAME */
	  "[%s] AS [m], "
	  /* CT_METHSIG_NAME */
	  "[%s] AS [s] "
	"WHERE "
	  "[s].[meth_of] = [m] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[m].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[m].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_METHOD_NAME,
	CT_METHSIG_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_method_argument_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[s].[meth_of].[meth_name] AS [meth_name], "
	  "[s].[meth_of].[class_of].[class_name] AS [class_name], "
	  "CAST ([s].[meth_of].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "CASE [s].[meth_of].[meth_type] WHEN 0 THEN 'INSTANCE' ELSE 'CLASS' END AS [meth_type], "
	  "[a].[index_of] AS [index_of], "
	  "[t].[type_name] AS [data_type], "
	  "[d].[prec] AS [prec], "
	  "[d].[scale] AS [scale], "
	  "[d].[code_set] AS [code_set], "
	  "[d].[class_of].[class_name] AS [domain_class_name], "
	  "CAST ([d].[class_of].[owner].[name] AS VARCHAR(255)) AS [domain_owner_name] " /* string -> varchar(255) */
	"FROM "
	  /* CT_METHSIG_NAME */
	  "[%s] AS [s], "
	  /* CT_METHARG_NAME */
	  "[%s] AS [a], "
	  /* CT_DOMAIN_NAME */
	  "[%s] AS [d], "
	  /* CT_DATATYPE_NAME */
	  "[%s] AS [t] "
	"WHERE "
	  "[a].[meth_sig_of] = [s] "
	  "AND [d].[object_of] = [a] "
	  "AND [d].[data_type] = [t].[type_id] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[s].[meth_of].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[s].[meth_of].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_METHSIG_NAME,
	CT_METHARG_NAME,
	CT_DOMAIN_NAME,
	CT_DATATYPE_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_method_argument_set_domain_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[s].[meth_of].[meth_name] AS [meth_name], "
	  "[s].[meth_of].[class_of].[class_name] AS [class_name], "
	  "CAST ([s].[meth_of].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "CASE [s].[meth_of].[meth_type] WHEN 0 THEN 'INSTANCE' ELSE 'CLASS' END AS [meth_type], "
	  "[a].[index_of] AS [index_of], "
	  "[et].[type_name] AS [data_type], "
	  "[e].[prec] AS [prec], "
	  "[e].[scale] AS [scale], "
	  "[e].[code_set] AS [code_set], "
	  "[e].[class_of].[class_name] AS [domain_class_name], "
	  "CAST ([e].[class_of].[owner].[name] AS VARCHAR(255)) AS [domain_owner_name] " /* string -> varchar(255) */
	"FROM "
	  /* CT_METHSIG_NAME */
	  "[%s] AS [s], "
	  /* CT_METHARG_NAME */
	  "[%s] AS [a], "
	  /* CT_DOMAIN_NAME */
	  "[%s] AS [d], TABLE ([d].[set_domains]) AS [t] ([e]), "
	  /* CT_DATATYPE_NAME */
	  "[%s] AS [et] "
	"WHERE "
	  "[a].[meth_sig_of] = [s] "
	  "AND [d].[object_of] = [a] "
	  "AND [e].[data_type] = [et].[type_id] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[s].[meth_of].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[s].[meth_of].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_METHSIG_NAME,
	CT_METHARG_NAME,
	CT_DOMAIN_NAME,
	CT_DATATYPE_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_method_file_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[f].[class_of].[class_name] AS [class_name], "
	  "CAST ([f].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[f].[path_name] AS [path_name], "
	  "[f].[from_class_of].[class_name] AS [from_class_name], "
	  "CAST ([f].[from_class_of].[owner].[name] AS VARCHAR(255)) AS [from_owner_name] " /* string -> varchar(255) */
	"FROM "
	  /* CT_METHFILE_NAME */
	  "[%s] AS [f] "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[f].[class_of].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[f].[class_of]} SUBSETEQ ("
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",
	CT_METHFILE_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_index_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[i].[index_name] AS [index_name], "
	  "CASE [i].[is_unique] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_unique], "
	  "CASE [i].[is_reverse] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_reverse], "
	  "[i].[class_of].[class_name] AS [class_name], "
	  "CAST ([i].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "NVL2 ("
	      "("
		"SELECT 1 "
		"FROM "
		  /* CT_INDEXKEY_NAME */
		  "[%s] [k] "
		"WHERE "
		  "[k].index_of.class_of = [i].class_of "
		  "AND [k].index_of.index_name = [i].[index_name] "
		  "AND [k].key_attr_name LIKE " DEDUPLICATE_KEY_ATTR_NAME_LIKE_PATTERN
	      "), "
	      "([i].[key_count] - 1), "
	      "[i].[key_count]"
	    ") AS [key_count], "        
	  "CASE [i].[is_primary_key] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_primary_key], "
	  "CASE [i].[is_foreign_key] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_foreign_key], "
#if 0 // Not yet, Disabled for QA verification convenience          
/* support for SUPPORT_DEDUPLICATE_KEY_MODE */
	  "CAST(NVL ("
                  "(" 
		      "SELECT 'YES' "
		      "FROM [%s] [k] "
		      "WHERE [k].index_of.class_of = [i].class_of "
			  "AND [k].index_of.index_name = [i].[index_name] "
			  "AND [k].key_attr_name LIKE " DEDUPLICATE_KEY_ATTR_NAME_LIKE_PATTERN
   		   "), "
                   "'NO') "
                   "AS VARCHAR(3)"
             ") AS [is_deduplicate], "
	  "CAST(NVL (" 
                  "("
		      "SELECT REPLACE([k].key_attr_name,'%s','') "
		      "FROM [%s] [k]"
		      "WHERE [k].index_of.class_of = [i].class_of "
			   "AND [k].index_of.index_name = [i].[index_name] "
			   "AND [k].key_attr_name LIKE " DEDUPLICATE_KEY_ATTR_NAME_LIKE_PATTERN
		   ")"
                   ", 0)" 
                  " AS SMALLINT" 
             ") AS [deduplicate_key_level], "
#endif
	  "[i].[filter_expression] AS [filter_expression], "
	  "CASE [i].[have_function] WHEN 0 THEN 'NO' ELSE 'YES' END AS [have_function], "
	  "[i].[comment] AS [comment], "
	  "CASE [i].[status] "
	    "WHEN 0 THEN 'NO_INDEX' "
	    "WHEN 1 THEN 'NORMAL INDEX' "
	    "WHEN 2 THEN 'INVISIBLE INDEX' "
	    "WHEN 3 THEN 'INDEX IS IN ONLINE BUILDING' "
	    "ELSE 'NULL' "
	    "END AS [status] "
	"FROM "
	  /* CT_INDEX_NAME */
	  "[%s] AS [i] "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[i].[class_of].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[i].[class_of]} SUBSETEQ ("
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",            
	CT_INDEXKEY_NAME,
#if 0 // Not yet, Disabled for QA verification convenience        
        CT_INDEXKEY_NAME,
        DEDUPLICATE_KEY_ATTR_NAME_PREFIX,
        CT_INDEXKEY_NAME,
#endif                    
	CT_INDEX_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_index_key_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[k].[index_of].[index_name] AS [index_name], "
	  "[k].[index_of].[class_of].[class_name] AS [class_name], "
	  "CAST ([k].[index_of].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[k].[key_attr_name] AS [key_attr_name], "
	  "[k].[key_order] AS [key_order], "
	  "CASE [k].[asc_desc] WHEN 0 THEN 'ASC' WHEN 1 THEN 'DESC' ELSE 'UNKN' END AS [asc_desc], "
	  "[k].[key_prefix_length] AS [key_prefix_length], "
	  "[k].[func] AS [func] "
	"FROM "
	  /* CT_INDEXKEY_NAME */
	  "[%s] AS [k] "
	"WHERE "
          "("
              "[k].[key_attr_name] IS NULL " 
              "OR [k].[key_attr_name] NOT LIKE " DEDUPLICATE_KEY_ATTR_NAME_LIKE_PATTERN
          ")"
          " AND ("       
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[k].[index_of].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[k].[index_of].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_INDEXKEY_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_authorization_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "CAST ([a].[grantor].[name] AS VARCHAR(255)) AS [grantor_name], " /* string -> varchar(255) */
	  "CAST ([a].[grantee].[name] AS VARCHAR(255)) AS [grantee_name], " /* string -> varchar(255) */
	  "[a].[class_of].[class_name] AS [class_name], "
	  "CAST ([a].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[a].[auth_type] AS [auth_type], "
	  "CASE [a].[is_grantable] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_grantable] "
	"FROM "
	  /* CT_CLASSAUTH_NAME */
	  "[%s] AS [a] "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[a].[class_of].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[a].[class_of]} SUBSETEQ ("
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_trigger_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "CAST ([t].[name] AS VARCHAR (255)) AS [trigger_name], " /* string -> varchar(255) */
	  "CAST ([t].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[c].[class_name] AS [target_class_name], "
	  "CAST ([c].[owner].[name] AS VARCHAR(255)) AS [target_owner_name], " /* string -> varchar(255) */
	  "CAST ([t].[target_attribute] AS VARCHAR (255)) AS [target_attr_name], " /* string -> varchar(255) */
	  "CASE [t].[target_class_attribute] WHEN 0 THEN 'INSTANCE' ELSE 'CLASS' END AS [target_attr_type], "
	  "[t].[action_type] AS [action_type], "
	  "[t].[action_time] AS [action_time], "
	  "[t].[comment] AS [comment] "
	"FROM "
	  /* TR_CLASS_NAME */
	  "[%s] AS [t] "
	  /* CT_CLASS_NAME */
	  "LEFT OUTER JOIN [%s] AS [c] ON [t].[target_class] = [c].[class_of] "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[t].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[c]} SUBSETEQ (" /* Why [c] and not [t].[target_class]? */
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",
	TR_CLASS_NAME,
	CT_CLASS_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_partition_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[s].[class_name] AS [class_name], "
	  "CAST ([s].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[p].[pname] AS [partition_name], "
	  "CONCAT ([s].[class_name], '__p__', [p].[pname]) AS [partition_class_name], " /* It can exceed varchar(255). */
	  "CASE [p].[ptype] WHEN 0 THEN 'HASH' WHEN 1 THEN 'RANGE' ELSE 'LIST' END AS [partition_type], "
	  "TRIM (SUBSTRING ([pp].[pexpr] FROM 8 FOR (POSITION (' FROM ' IN [pp].[pexpr]) - 8))) AS [partition_expr], "
	  "[p].[pvalues] AS [partition_values], "
	  "[p].[comment] AS [comment] "
	"FROM "
	  /* CT_PARTITION_NAME */
	  "[%s] AS [p], "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], TABLE ([c].[super_classes]) AS [t] ([s]), "
	  /* CT_CLASS_NAME */
	  "[%s] AS [cc], TABLE ([cc].[partition]) AS [tt] ([pp]) "
	"WHERE "
	  "[p].[class_of] = [c] "
	  "AND [s] = [cc] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[p].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[p].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_PARTITION_NAME,
	CT_CLASS_NAME,
	CT_CLASS_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_stored_procedure_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[sp].[sp_name] AS [sp_name], "
	  "CASE [sp].[sp_type] WHEN 1 THEN 'PROCEDURE' ELSE 'FUNCTION' END AS [sp_type], "
	  "CASE [sp].[return_type] "
	    "WHEN 0 THEN 'void' "
	    "WHEN 28 THEN 'CURSOR' "
	    /* CT_DATATYPE_NAME */
	    "ELSE (SELECT [t].[type_name] FROM [%s] AS [t] WHERE [sp].[return_type] = [t].[type_id]) "
	    "END AS [return_type], "
	  "[sp].[arg_count] AS [arg_count], "
	  "CASE [sp].[lang] WHEN 1 THEN 'JAVA' ELSE '' END AS [lang], "
	  "[sp].[target] AS [target], "
	  "CAST ([sp].[owner].[name] AS VARCHAR(255)) AS [owner], " /* string -> varchar(255) */
	  "[sp].[comment] AS [comment] "
	"FROM "
	  /* CT_STORED_PROC_NAME */
	  "[%s] AS [sp]",
	CT_DATATYPE_NAME,
	CT_STORED_PROC_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_stored_procedure_arguments_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[sp].[sp_name] AS [sp_name], "
	  "[sp].[index_of] AS [index_of], "
	  "[sp].[arg_name] AS [arg_name], "
	  "CASE [sp].[data_type] "
	    "WHEN 28 THEN 'CURSOR' "
	    /* CT_DATATYPE_NAME */
	    "ELSE (SELECT [t].[type_name] FROM [%s] AS [t] WHERE [sp].[data_type] = [t].[type_id]) "
	    "END AS [data_type], "
	  "CASE [sp].[mode] WHEN 1 THEN 'IN' WHEN 2 THEN 'OUT' ELSE 'INOUT' END AS [mode], "
	  "[sp].[comment] AS [comment] "
	"FROM "
	  /* CT_STORED_PROC_ARGS_NAME */
	  "[%s] AS [sp] "
	"ORDER BY " /* Is it possible to remove ORDER BY? */
	  "[sp].[sp_name], "
	  "[sp].[index_of]",
	CT_DATATYPE_NAME,
	CT_STORED_PROC_ARGS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_db_collation_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[coll].[coll_id] AS [coll_id], "
	  "[coll].[coll_name] AS [coll_name], "
	  "[ch].[charset_name] AS [charset_name], "
	  "CASE [coll].[built_in] WHEN 0 THEN 'No' WHEN 1 THEN 'Yes' ELSE 'ERROR' END AS [is_builtin], "
	  "CASE [coll].[expansions] WHEN 0 THEN 'No' WHEN 1 THEN 'Yes' ELSE 'ERROR' END AS [has_expansions], "
	  "[coll].[contractions] AS [contractions], "
	  "CASE [coll].[uca_strength] "
	    "WHEN 0 THEN 'Not applicable' "
	    "WHEN 1 THEN 'Primary' "
	    "WHEN 2 THEN 'Secondary' "
	    "WHEN 3 THEN 'Tertiary' "
	    "WHEN 4 THEN 'Quaternary' "
	    "WHEN 5 THEN 'Identity' "
	    "ELSE 'Unknown' "
	    "END AS [uca_strength] "
	"FROM "
	  /* CT_COLLATION_NAME */
	  "[%s] AS [coll] "
	  /* CT_CHARSET_NAME */
	  "INNER JOIN [%s] AS [ch] ON [coll].[charset_id] = [ch].[charset_id] "
	"ORDER BY " /* Is it possible to remove ORDER BY? */
	  "[coll].[coll_id]",
	CT_COLLATION_NAME,
	CT_CHARSET_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_db_charset_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[ch].[charset_id] AS [charset_id], "
	  "[ch].[charset_name] AS [charset_name], "
	  "[coll].[coll_name] AS [default_collation], "
	  "[ch].[char_size] AS [char_size] " 
	"FROM "
	  /* CT_CHARSET_NAME */
	  "[%s] AS [ch], "
	  /* CT_COLLATION_NAME */
	  "[%s] AS [coll] "
	"WHERE "
	  "[ch].[default_collation] = [coll].[coll_id] "
	"ORDER BY " /* Is it possible to remove ORDER BY? */
	  "[ch].[charset_id]",
	CT_CHARSET_NAME,
	CT_COLLATION_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_synonym_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[s].[name] AS [synonym_name], "
	  "CAST ([s].[owner].[name] AS VARCHAR(255)) AS [synonym_owner_name], " /* string -> varchar(255) */
	  "CASE [s].[is_public] WHEN 1 THEN 'YES' ELSE 'NO' END AS [is_public_synonym], "
	  "[s].[target_name] AS [target_name], "
	  "CAST ([s].[target_owner].[name] AS VARCHAR(255)) AS [target_owner_name], " /* string -> varchar(255) */
	  "[s].[comment] AS [comment] "
	"FROM "
	  /* CT_SYNONYM_NAME */
	  "[%s] AS [s] "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR [s].[is_public] = 1 "
	  "OR ("
	      "[s].[is_public] = 0 "
	      "AND {[s].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		")"
	    ")",
	CT_SYNONYM_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

static const char *
sm_define_view_db_server_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[ds].[link_name] AS [link_name], "
	  "[ds].[host] AS [host], "
	  "[ds].[port] AS [port], "
	  "[ds].[db_name] AS [db_name], "
	  "[ds].[user_name] AS [user_name], "
	  "[ds].[properties] AS [properties], "
	  "CAST ([ds].[owner].[name] AS VARCHAR(255)) AS [owner], " /* string -> varchar(255) */
	  "[ds].[comment] AS [comment] "
	"FROM "
	  /* CT_DB_SERVER_NAME */
	  "[%s] AS [ds] "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[ds].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[ds]} SUBSETEQ ("
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",
	CT_DB_SERVER_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}
