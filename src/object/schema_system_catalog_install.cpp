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

#include "db.h"
#include "dbtype_function.h"
#include "transform.h"
#include "work_space.h"
#include "schema_manager.h"
#include "schema_system_catalog_builder.hpp"
#include "schema_system_catalog_definition.hpp"
#include "authenticate.h"
#include "locator_cl.h"

// system classes
static int boot_define_class (MOP class_mop);
static int boot_define_attribute (MOP class_mop);
static int boot_define_domain (MOP class_mop);
static int boot_define_method (MOP class_mop);
static int boot_define_meth_sig (MOP class_mop);
static int boot_define_meth_argument (MOP class_mop);
static int boot_define_meth_file (MOP class_mop);
static int boot_define_query_spec (MOP class_mop);
static int boot_define_index (MOP class_mop);
static int boot_define_index_key (MOP class_mop);
static int boot_define_class_authorization (MOP class_mop);
static int boot_define_partition (MOP class_mop);
static int boot_add_data_type (MOP class_mop);
static int boot_define_data_type (MOP class_mop);
static int boot_define_stored_procedure (MOP class_mop);
static int boot_define_stored_procedure_arguments (MOP class_mop);
static int boot_define_serial (MOP class_mop);
static int boot_define_ha_apply_info (MOP class_mop);
static int boot_define_collations (MOP class_mop);
static int boot_add_charsets (MOP class_mop);
static int boot_define_charsets (MOP class_mop);
static int boot_define_dual (MOP class_mop);
static int boot_define_db_server (MOP class_mop);
static int boot_define_synonym (MOP class_mop);

// system vclasses
static int boot_define_view_class (void);
static int boot_define_view_super_class (void);
static int boot_define_view_vclass (void);
static int boot_define_view_attribute (void);
static int boot_define_view_attribute_set_domain (void);
static int boot_define_view_method (void);
static int boot_define_view_method_argument (void);
static int boot_define_view_method_argument_set_domain (void);
static int boot_define_view_method_file (void);
static int boot_define_view_index (void);
static int boot_define_view_index_key (void);
static int boot_define_view_authorization (void);
static int boot_define_view_trigger (void);
static int boot_define_view_partition (void);
static int boot_define_view_stored_procedure (void);
static int boot_define_view_stored_procedure_arguments (void);
static int boot_define_view_db_collation (void);
static int boot_define_view_db_charset (void);
static int boot_define_view_db_server (void);
static int boot_define_view_synonym (void);

/* ========================================================================== */
/* NEW DEFINITION */
/* ========================================================================== */

namespace cubschema
{
// TODO: find right place
// TODO: implement formatting utility function for std::string (like fmt library)
  const inline std::string format_varchar (const int size)
  {
    std::string s ("varchar(");
    s += std::to_string (size);
    s += ")";
    return s;
  }

  const inline std::string format_sequence (const std::string_view type)
  {
    std::string s ("sequence of ");
    s.append (type);
    return s;
  }

  const std::string OBJECT {"object"};
  const std::string INTEGER {"integer"};
  const std::string VARCHAR_255 {format_varchar (255)};
  const std::string VARCHAR_2048 {format_varchar (2048)}; // for comment

// _db_class
  system_catalog_definition sm_define_class (
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
  }
  );
}

// for backward compatibility
using COLUMN = cubschema::column;

/* ========================================================================== */
/* MAIN APIS */
/* ========================================================================== */

static cubschema::catcls_function clist[] =
{
  // {CT_CLASS_NAME, boot_define_class},
  {CT_ATTRIBUTE_NAME, boot_define_attribute},
  {CT_DOMAIN_NAME, boot_define_domain},
  {CT_METHOD_NAME, boot_define_method},
  {CT_METHSIG_NAME, boot_define_meth_sig},
  {CT_METHARG_NAME, boot_define_meth_argument},
  {CT_METHFILE_NAME, boot_define_meth_file},
  {CT_QUERYSPEC_NAME, boot_define_query_spec},
  {CT_INDEX_NAME, boot_define_index},
  {CT_INDEXKEY_NAME, boot_define_index_key},
  {CT_DATATYPE_NAME, boot_define_data_type},
  {CT_CLASSAUTH_NAME, boot_define_class_authorization},
  {CT_PARTITION_NAME, boot_define_partition},
  {CT_STORED_PROC_NAME, boot_define_stored_procedure},
  {CT_STORED_PROC_ARGS_NAME, boot_define_stored_procedure_arguments},
  {CT_SERIAL_NAME, boot_define_serial},
  {CT_HA_APPLY_INFO_NAME, boot_define_ha_apply_info},
  {CT_COLLATION_NAME, boot_define_collations},
  {CT_CHARSET_NAME, boot_define_charsets},
  {CT_DUAL_NAME, boot_define_dual},
  {CT_DB_SERVER_NAME, boot_define_db_server},
  {CT_SYNONYM_NAME, boot_define_synonym}
};

static cubschema::catcls_function_ng clist_new [] =
{
  {CT_CLASS_NAME, cubschema::sm_define_class}
};

static cubschema::catcls_function vclist[] =
{
  {CTV_CLASS_NAME, boot_define_view_class},
  {CTV_SUPER_CLASS_NAME, boot_define_view_super_class},
  {CTV_VCLASS_NAME, boot_define_view_vclass},
  {CTV_ATTRIBUTE_NAME, boot_define_view_attribute},
  {CTV_ATTR_SD_NAME, boot_define_view_attribute_set_domain},
  {CTV_METHOD_NAME, boot_define_view_method},
  {CTV_METHARG_NAME, boot_define_view_method_argument},
  {CTV_METHARG_SD_NAME, boot_define_view_method_argument_set_domain},
  {CTV_METHFILE_NAME, boot_define_view_method_file},
  {CTV_INDEX_NAME, boot_define_view_index},
  {CTV_INDEXKEY_NAME, boot_define_view_index_key},
  {CTV_AUTH_NAME, boot_define_view_authorization},
  {CTV_TRIGGER_NAME, boot_define_view_trigger},
  {CTV_PARTITION_NAME, boot_define_view_partition},
  {CTV_STORED_PROC_NAME, boot_define_view_stored_procedure},
  {CTV_STORED_PROC_ARGS_NAME, boot_define_view_stored_procedure_arguments},
  {CTV_DB_COLLATION_NAME, boot_define_view_db_collation},
  {CTV_DB_CHARSET_NAME, boot_define_view_db_charset},
  {CTV_DB_SERVER_NAME, boot_define_view_db_server},
  {CTV_SYNONYM_NAME,boot_define_view_synonym}
};

int
catcls_install_class (void)
{
  int error_code = NO_ERROR;

  int num_classes_old = sizeof (clist) / sizeof (clist[0]);
  int num_classes_new = sizeof (clist_new) / sizeof (clist_new[0]);
  int num_classes_total = num_classes_old + num_classes_new;

  MOP class_mop[num_classes_total];
  int i, save;
  AU_DISABLE (save);

  using catalog_builder = cubschema::system_catalog_builder;

  for (i = 0; i < num_classes_total; i++)
    {
      MOP mop = nullptr;
      if (i < num_classes_new)
	{
	  // new routine
	  mop = class_mop[i] = catalog_builder::create_and_mark_system_class (clist_new[i].name);
	}
      else
	{
	  // old routine
	  int idx = i - num_classes_new;
	  mop = class_mop[i] = db_create_class (clist[idx].name);
	}
      if (mop == nullptr)
	{
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();
	  goto end;
	}
      sm_mark_system_class (mop, 1);
    }

  for (i = 0; i < num_classes_total; i++)
    {
      if (i < num_classes_new)
	{
	  // new routine
	  error_code = catalog_builder::build_class (class_mop[i], clist_new[i].definition);
	}
      else
	{
	  // old routine
	  int idx = i - num_classes_new;
	  error_code = (clist[idx].class_func) (class_mop[i]);
	}

      if (error_code != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
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
  size_t num_vclasses = sizeof (vclist) / sizeof (vclist[0]);
  int error_code = NO_ERROR;

  AU_DISABLE (save);

  for (i = 0; i < num_vclasses; i++)
    {
      error_code = (vclist[i].vclass_func) ();
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
/* LEGACY FUNCTIONS (SYSTEM CLASS) */
/* ========================================================================== */

/*
 * boot_define_class :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_class (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *index1_col_names[2] = { "unique_name", NULL };
  const char *index2_col_names[3] = { "class_name", "owner", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", "object", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* unique name */
  error_code = smt_add_attribute (def, "unique_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* class name */
  error_code = smt_add_attribute (def, "class_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_system_class", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "owner", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "inst_attr_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_attr_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "shared_attr_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "inst_meth_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_meth_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "collation_id", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "tde_algorithm", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_CLASS_NAME);

  error_code = smt_add_attribute (def, "sub_classes", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "super_classes", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_ATTRIBUTE_NAME);

  error_code = smt_add_attribute (def, "inst_attrs", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_attrs", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "shared_attrs", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_METHOD_NAME);

  error_code = smt_add_attribute (def, "inst_meths", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_meths", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_METHFILE_NAME);

  error_code = smt_add_attribute (def, "meth_files", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_QUERYSPEC_NAME);

  error_code = smt_add_attribute (def, "query_specs", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_INDEX_NAME);

  error_code = smt_add_attribute (def, "indexes", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(2048)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_PARTITION_NAME);

  error_code = smt_add_attribute (def, "partition", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

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
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, "i__db_class_unique_name", index1_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index2_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_attribute :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_attribute (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *index_col_names[4] = { "class_of", "attr_name", "attr_type", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "attr_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "attr_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "from_class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "from_attr_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "def_order", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "data_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "default_value", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_DOMAIN_NAME);

  error_code = smt_add_attribute (def, "domains", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_nullable", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_domain :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 *
 * Note:
 *
 */
static int
boot_define_domain (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *index_col_names1[2] = { "object_of", NULL };
  const char *index_col_names2[2] = { "data_type", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "object_of", "object", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "data_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "prec", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "scale", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "code_set", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "collation_id", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "enumeration", "sequence of character varying", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_DOMAIN_NAME);

  error_code = smt_add_attribute (def, "set_domains", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "json_schema", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names1, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names2, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_method :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_method (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *names[3] = { "class_of", "meth_name", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "meth_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "meth_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "from_class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "from_meth_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_METHSIG_NAME);

  error_code = smt_add_attribute (def, "signatures", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_meth_sig :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_meth_sig (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *names[2] = { "meth_of", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "meth_of", CT_METHOD_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "func_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "arg_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_METHARG_NAME);

  error_code = smt_add_attribute (def, "return_value", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "arguments", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_meth_argument :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_meth_argument (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "meth_sig_of", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "meth_sig_of", CT_METHSIG_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "data_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "index_of", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_DOMAIN_NAME);

  error_code = smt_add_attribute (def, "domains", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_meth_file :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_meth_file (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "class_of", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "from_class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "path_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_query_spec :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_query_spec (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "class_of", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "spec", "varchar(1073741823)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_index :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_index (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "class_of", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "index_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_unique", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "key_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "sequence of %s", CT_INDEXKEY_NAME);

  error_code = smt_add_attribute (def, "key_attrs", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_reverse", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_primary_key", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_foreign_key", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "filter_expression", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "have_function", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "status", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_meth_argument :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_index_key (MOP class_mop)
{
  SM_TEMPLATE *def;
  DB_VALUE prefix_default;
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "index_of", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = smt_add_attribute (def, "index_of", CT_INDEX_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "key_attr_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "key_order", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "asc_desc", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "key_prefix_length", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  db_make_int (&prefix_default, -1);

  error_code = smt_set_attribute_default (def, "key_prefix_length", 0, &prefix_default, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "func", "varchar(1023)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_class_authorization :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_class_authorization (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "grantee", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "grantor", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "grantee", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "auth_type", "varchar(7)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_grantable", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_partition :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_partition (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[] = { "class_of", "pname", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "class_of", CT_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "pname", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "ptype", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "pexpr", "varchar(2048)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "pvalues", "sequence of", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_add_data_type :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 *
 * Note:
 *
 */
static int
boot_add_data_type (MOP class_mop)
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

/*
 * boot_define_data_type :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_data_type (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "type_id", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* TODO : DB migration tool */
  error_code = smt_add_attribute (def, "type_name", "varchar(16)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = boot_add_data_type (class_mop);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_stored_procedure :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_stored_procedure (MOP class_mop)
{
  SM_TEMPLATE *def;
  char args_string[64];
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "sp_name", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "sp_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "sp_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "return_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "arg_count", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (args_string, "sequence of %s", CT_STORED_PROC_ARGS_NAME);
  error_code = smt_add_attribute (def, "args", args_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "lang", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "target", "varchar(4096)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "owner", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_UNIQUE, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_stored_procedure_arguments :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_stored_procedure_arguments (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[2] = { "sp_name", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "sp_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "index_of", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "arg_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "data_type", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "mode", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_serial :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_serial (MOP class_mop)
{
  SM_TEMPLATE *def;
  char domain_string[32];
  unsigned char num[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  DB_VALUE default_value;
  int error_code = NO_ERROR;
  const char *index1_col_names[] = { "unique_name", NULL };
  const char *index2_col_names[] = { "name", "owner", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "unique_name", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "name", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "owner", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "numeric(%d,0)", DB_MAX_NUMERIC_PRECISION);
  numeric_coerce_int_to_num (1, num);
  db_make_numeric (&default_value, num, DB_MAX_NUMERIC_PRECISION, 0);

  error_code = smt_add_attribute (def, "current_val", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "current_val", 0, &default_value, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "increment_val", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "increment_val", 0, &default_value, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "max_val", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "min_val", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  db_make_int (&default_value, 0);

  error_code = smt_add_attribute (def, "cyclic", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "cyclic", 0, &default_value, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "started", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "started", 0, &default_value, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_name", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "att_name", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_class_method (def, "change_serial_owner", "au_change_serial_owner_method");
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "cached_num", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "cached_num", 0, &default_value, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code =
	  db_add_constraint (class_mop, DB_CONSTRAINT_PRIMARY_KEY, "pk_db_serial_unique_name", index1_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_UNIQUE, NULL, index2_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "current_val", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "increment_val", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "max_val", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "min_val", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_ha_apply_info :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_ha_apply_info (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[] = { "db_name", "copied_log_path", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "db_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "db_creation_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "copied_log_path", "varchar(4096)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "committed_lsa_pageid", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "committed_lsa_offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "committed_rep_pageid", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "committed_rep_offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "append_lsa_pageid", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "append_lsa_offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "eof_lsa_pageid", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "eof_lsa_offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "final_lsa_pageid", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "final_lsa_offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "required_lsa_pageid", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "required_lsa_offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "log_record_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "log_commit_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "last_access_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "status", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "insert_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "update_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "delete_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "schema_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "commit_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "fail_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "start_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add constraints */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_UNIQUE, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "db_name", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "copied_log_path", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "committed_lsa_pageid", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "committed_lsa_offset", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "required_lsa_pageid", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "required_lsa_offset", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_add_collations :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 *
 * Note:
 *
 */
int
boot_add_collations (MOP class_mop)
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

/*
 * boot_define_collations :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_collations (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, CT_DBCOLL_COLL_ID_COLUMN, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_COLL_NAME_COLUMN, "varchar(32)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_CHARSET_ID_COLUMN, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_BUILT_IN_COLUMN, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_EXPANSIONS_COLUMN, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_CONTRACTIONS_COLUMN, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_UCA_STRENGTH, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCOLL_CHECKSUM_COLUMN, "varchar(32)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = boot_add_collations (class_mop);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

#define CT_DBCHARSET_CHARSET_ID		  "charset_id"
#define CT_DBCHARSET_CHARSET_NAME	  "charset_name"
#define CT_DBCHARSET_DEFAULT_COLLATION	  "default_collation"
#define CT_DBCHARSET_CHAR_SIZE		  "char_size"

/*
 * boot_add_charsets :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 *
 * Note:
 *
 */
static int
boot_add_charsets (MOP class_mop)
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

/*
 * boot_define_charsets :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_charsets (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, CT_DBCHARSET_CHARSET_ID, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCHARSET_CHARSET_NAME, "varchar(32)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCHARSET_DEFAULT_COLLATION, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, CT_DBCHARSET_CHAR_SIZE, "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = boot_add_charsets (class_mop);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

#define CT_DUAL_DUMMY   "dummy"

/*
 * boot_define_dual :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */

static int
boot_define_dual (MOP class_mop)
{
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  DB_OBJECT *obj;
  DB_VALUE val;
  const char *dummy = "X";

  def = smt_edit_class_mop (class_mop, AU_ALTER);
  if (def == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = smt_add_attribute (def, CT_DUAL_DUMMY, "varchar(1)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  obj = db_create_internal (class_mop);
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

  return NO_ERROR;
}

static int
boot_define_synonym (MOP class_mop)
{
  SM_TEMPLATE *def;
  DB_VALUE default_value;
  int error_code = NO_ERROR;
  const char *primary_key_col_names[] = { "unique_name", NULL };
  const char *index1_col_names[] = { "name", "owner", "is_public", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "unique_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "owner", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "is_public", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  db_make_int (&default_value, 0);

  error_code = smt_set_attribute_default (def, "is_public", 0, &default_value, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "target_unique_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "target_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "target_owner", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(2048)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add constraints */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_PRIMARY_KEY, NULL, primary_key_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_INDEX, NULL, index1_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "name", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "owner", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "is_public", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "target_unique_name", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "target_name", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_constrain_non_null (class_mop, "target_owner", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_db_server :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class(IN) :
 */
static int
boot_define_db_server (MOP class_mop)
{
  SM_TEMPLATE *def;
  char args_string[64];
  int error_code = NO_ERROR;
  const char *index_col_names[3] = { "link_name", "owner", NULL };

  def = smt_edit_class_mop (class_mop, AU_ALTER);

  error_code = smt_add_attribute (def, "link_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "host", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "port", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "db_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "user_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "password", "string", (DB_DOMAIN *) 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "properties", "varchar(2048)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "owner", AU_USER_CLASS_NAME, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_update_class (def, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = db_add_constraint (class_mop, DB_CONSTRAINT_PRIMARY_KEY, NULL, index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/* ========================================================================== */
/* LEGACY FUNCTIONS (SYSTEM VCLASS) */
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

/*
 * boot_define_view_class :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 */
static int
boot_define_view_class (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"class_type", "varchar(6)"},
    {"is_system_class", "varchar(3)"},
    {"tde_algorithm", "varchar(32)"},
    {"partitioned", "varchar(3)"},
    {"is_reuse_oid_class", "varchar(3)"},
    {"collation", "varchar(32)"},
    {"comment", "varchar(2048)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_CLASS_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_super_class :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_super_class (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"super_class_name", "varchar(255)"},
    {"super_owner_name", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_SUPER_CLASS_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_vclass :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_vclass (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"vclass_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"vclass_def", "varchar(1073741823)"},
    {"comment", "varchar(2048)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_VCLASS_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_attribute :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_attribute (void)
{
  MOP class_mop;
  COLUMN columns[] =
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
    {"comment", "varchar(1024)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_ATTRIBUTE_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_attribute_set_domain :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_attribute_set_domain (void)
{
  MOP class_mop;
  COLUMN columns[] =
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
    {"domain_owner_name", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_ATTR_SD_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_method :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_method (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"meth_name", "varchar(255)"},
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"meth_type", "varchar(8)"},
    {"from_class_name", "varchar(255)"},
    {"from_owner_name", "varchar(255)"},
    {"from_meth_name", "varchar(255)"},
    {"func_name", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_METHOD_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_method_argument :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_method_argument (void)
{
  MOP class_mop;
  COLUMN columns[] =
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
    {"domain_owner_name", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_METHARG_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_method_argument_set_domain :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 * Note:
 *
 */
static int
boot_define_view_method_argument_set_domain (void)
{
  MOP class_mop;
  COLUMN columns[] =
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
    {"domain_owner_name", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_METHARG_SD_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_method_file :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_method_file (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"path_name", "varchar(255)"},
    {"from_class_name", "varchar(255)"},
    {"from_owner_name", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_METHFILE_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_index :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_index (void)
{
  MOP class_mop;
  COLUMN columns[] =
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
#if defined(SUPPORT_DEDUPLICATE_KEY_MODE)
    {"is_deduplicate", "varchar(3)"},
    {"deduplicate_key_level", "smallint"},
#endif
#endif
    {"filter_expression", "varchar(255)"},
    {"have_function", "varchar(3)"},
    {"comment", "varchar(1024)"},
    {"status", "varchar(255)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_INDEX_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[i].[index_name] AS [index_name], "
	  "CASE [i].[is_unique] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_unique], "
	  "CASE [i].[is_reverse] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_reverse], "
	  "[i].[class_of].[class_name] AS [class_name], "
	  "CAST ([i].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
#if defined(SUPPORT_DEDUPLICATE_KEY_MODE)
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
#else
	  "[i].[key_count] AS [key_count], "
#endif          
	  "CASE [i].[is_primary_key] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_primary_key], "
	  "CASE [i].[is_foreign_key] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_foreign_key], "
#if 0 // Not yet, Disabled for QA verification convenience          
#if defined(SUPPORT_DEDUPLICATE_KEY_MODE)
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
#if defined(SUPPORT_DEDUPLICATE_KEY_MODE)
	CT_INDEXKEY_NAME,
#if 0 // Not yet, Disabled for QA verification convenience        
        CT_INDEXKEY_NAME,
        DEDUPLICATE_KEY_ATTR_NAME_PREFIX,
        CT_INDEXKEY_NAME,
#endif        
#endif            
	CT_INDEX_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_index_key :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_index_key (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"index_name", "varchar(255)"},
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"key_attr_name", "varchar(255)"},
    {"key_order", "integer"},
    {"asc_desc", "varchar(4)"},
    {"key_prefix_length", "integer"},
    {"func", "varchar(1023)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_INDEXKEY_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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
#if defined(SUPPORT_DEDUPLICATE_KEY_MODE)
          "("
              "[k].[key_attr_name] IS NULL " 
              "OR [k].[key_attr_name] NOT LIKE " DEDUPLICATE_KEY_ATTR_NAME_LIKE_PATTERN
          ")"
          " AND ("
#endif        
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
#if defined(SUPPORT_DEDUPLICATE_KEY_MODE)
		")"
#endif
	    ")",
	CT_INDEXKEY_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_authorization :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_authorization (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"grantor_name", "varchar(255)"},
    {"grantee_name", "varchar(255)"},
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"auth_type", "varchar(7)"},
    {"is_grantable", "varchar(3)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_AUTH_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  /* When a user is granted SELECT privilege,
   * that user can also view the list of privileges that other users have been granted.
   * Is this no problem? */

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_trigger :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_trigger (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"trigger_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"target_class_name", "varchar(255)"},
    {"target_owner_name", "varchar(255)"},
    {"target_attr_name", "varchar(255)"},
    {"target_attr_type", "varchar(8)"},
    {"action_type", "integer"},
    {"action_time", "integer"},
    {"comment", "varchar(1024)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_TRIGGER_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_partition :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_partition (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"class_name", "varchar(255)"},
    {"owner_name", "varchar(255)"},
    {"partition_name", "varchar(255)"},
    {"partition_class_name", "varchar(255)"},
    {"partition_type", "varchar(32)"},
    {"partition_expr", "varchar(2048)"},
    {"partition_values", "sequence of"},
    {"comment", "varchar(1024)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_PARTITION_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_stored_procedure :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_stored_procedure (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"sp_name", "varchar(255)"},
    {"sp_type", "varchar(16)"},
    {"return_type", "varchar(16)"},
    {"arg_count", "integer"},
    {"lang", "varchar(16)"},
    {"target", "varchar(4096)"},
    {"owner", "varchar(256)"},
    {"comment", "varchar(1024)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_STORED_PROC_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_stored_procedure_arguments :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_stored_procedure_arguments (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"sp_name", "varchar(255)"},
    {"index_of", "integer"},
    {"arg_name", "varchar(255)"},
    {"data_type", "varchar(16)"},
    {"mode", "varchar(6)"},
    {"comment", "varchar(1024)"}
  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_STORED_PROC_ARGS_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_db_collation :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_db_collation (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"coll_id", "integer"},
    {"coll_name", "varchar(32)"},
    {"charset_name", "varchar(32)"},
    {"is_builtin", "varchar(3)"},
    {"has_expansions", "varchar(3)"},
    {"contractions", "integer"},
    {"uca_strength", "varchar(255)"}
  };

  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_DB_COLLATION_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_db_charset :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_db_charset (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {CT_DBCHARSET_CHARSET_ID, "integer"},
    {CT_DBCHARSET_CHARSET_NAME, "varchar(32)"},
    {CT_DBCHARSET_DEFAULT_COLLATION, "varchar(32)"},
    {CT_DBCHARSET_CHAR_SIZE, "int"}
  };

  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  class_mop = db_create_vclass (CTV_DB_CHARSET_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

static int
boot_define_view_synonym (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"synonym_name", "varchar(255)"},
    {"synonym_owner_name", "varchar(255)"},
    {"is_public_synonym", "varchar(3)"},	/* access_modifier */
    {"target_name", "varchar(255)"},
    {"target_owner_name", "varchar(255)"},
    {"comment", "varchar(2048)"}
  };

  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;

  /* Initialization */
  memset (stmt, '\0', sizeof (char) * 2048);

  class_mop = db_create_vclass (CTV_SYNONYM_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

/*
 * boot_define_view_db_server :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_define_view_db_server (void)
{
  MOP class_mop;
  COLUMN columns[] =
  {
    {"link_name", "varchar(255)"},
    {"host", "varchar(255)"},
    {"port", "integer"},
    {"db_name", "varchar(255)"},
    {"user_name", "varchar(255)"},
    // {"password", "varchar(256)"}
    {"properties", "varchar(2048)"},
    {"owner", "varchar(255)"},
    {"comment", "varchar(1024)"}

  };
  int num_cols = sizeof (columns) / sizeof (columns[0]);
  int i;
  char stmt[2048];
  int error_code = NO_ERROR;


  class_mop = db_create_vclass (CTV_DB_SERVER_NAME);
  if (class_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  for (i = 0; i < num_cols; i++)
    {
      error_code = db_add_attribute (class_mop, columns[i].name.data (), columns[i].type.data (), NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

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

  error_code = db_add_query_spec (class_mop, stmt);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_change_owner (class_mop, Au_dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = au_grant (Au_public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}
