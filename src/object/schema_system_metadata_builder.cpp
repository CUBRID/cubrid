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

#include "schema_system_metadata_builder.hpp"

#include "db.h"
#include "dbtype.h"
#include "authenticate.h"
#include "class_object.h"
#include "schema_template.h"
#include "schema_information_schema.hpp"
#include "schema_manager.h"
#include "locator_cl.h"
#include "schema_system_catalog.hpp"

namespace cubschema
{
  MOP
  system_metadata_builder::create_and_mark_system_class (const std::string_view name)
  {
    if (name.empty ())
      {
	assert (false);
	return nullptr;
      }

    MOP class_mop = nullptr;
    if (sm_is_system_class (name))
      {
	class_mop = db_create_class (name.data ());
      }
    /* an information_schema view is created under its bare name and owned by INFORMATION_SCHEMA afterwards */
    else if (sm_is_system_vclass (name) || sm_is_information_schema_views (name))
      {
	class_mop = db_create_vclass (name.data ());
      }
    else
      {
	assert (false);
      }

    if (class_mop != nullptr)
      {
	sm_mark_system_class (class_mop, 1);
      }

    return class_mop;
  }

  int
  system_metadata_builder::build_class (const MOP class_mop, const system_metadata_definition &def)
  {
    int error_code = NO_ERROR;

    if (class_mop == nullptr)
      {
	// should not happen
	assert (false);
	return ER_FAILED;
      }

    // define
    SM_TEMPLATE *tmpl = smt_edit_class_mop (class_mop, AU_ALTER);
    if (tmpl == nullptr)
      {
	ASSERT_ERROR_AND_SET (error_code);
	return error_code;
      }

    const std::vector <attribute> &attributes = def.attributes;
    for (const auto &attr: attributes)
      {
	const char *name = attr.name.data ();
	const char *type = attr.type.data ();

	switch (attr.kind)
	  {
	  case attribute_kind::COLUMN:
	  {
	    error_code = smt_add_attribute (tmpl, name, type, NULL);
	    if (error_code != NO_ERROR)
	      {
		smt_quit (tmpl);
		return error_code;
	      }

	    if (attr.dvalue_func)
	      {
		DB_VALUE default_value;
		db_make_null (&default_value);
		attr.dvalue_func (&default_value);
		error_code = smt_set_attribute_default (tmpl, name, 0, (DB_VALUE *) &default_value, NULL);
	      }
	  }
	  break;
	  case attribute_kind::CLASS_METHOD:
	    error_code = smt_add_class_method (tmpl, name, type);
	    break;
	  default:
	    error_code = ER_FAILED;
	    break;
	  }

	if (error_code != NO_ERROR)
	  {
	    assert (false);
	    smt_quit (tmpl);
	    return error_code;
	  }
      }

    error_code = sm_update_class (tmpl, NULL);
    if (error_code != NO_ERROR)
      {
	assert (false);
	smt_quit (tmpl);
	return error_code;
      }

    const std::vector <constraint> &constraints = def.constraints;
    for (const auto &c: constraints)
      {
	DB_CONSTRAINT_TYPE type = c.type;

	const char *name = nullptr;
	if (!c.name.empty ())
	  {
	    name = c.name.data();
	  }

	int is_class_attribute = c.is_class_attributes ? 1 : 0;

	switch (type)
	  {
	  case DB_CONSTRAINT_INDEX:
	  case DB_CONSTRAINT_PRIMARY_KEY:
	  case DB_CONSTRAINT_UNIQUE:
	  case DB_CONSTRAINT_NOT_NULL:
	  {
	    error_code = db_add_constraint (class_mop, type, name, (const char **) c.attribute_names.data(),
					    is_class_attribute);
	  }
	  break;

	  default:
	    error_code = ER_FAILED;
	    break;
	  }

	if (error_code != NO_ERROR)
	  {
	    assert (false);
	    return error_code;
	  }
      }

    if (locator_has_heap (class_mop) == NULL)
      {
	assert (er_errid () != NO_ERROR);
	return er_errid ();
      }

    const authorization &auth = def.auth;
    if (auth.owner == nullptr)
      {
	assert (false);
	return ER_FAILED;
      }

    error_code = au_change_class_owner_including_partitions (class_mop, auth.owner);
    if (error_code != NO_ERROR)
      {
	assert (false);
	return error_code;
      }

    for (const grant &g : auth.grants)
      {
	error_code = au_grant (DB_OBJECT_CLASS, g.target_user, class_mop, g.auth, g.with_grant_option);
	if (error_code != NO_ERROR)
	  {
	    assert (false);
	    return error_code;
	  }
      }

    if (def.row_initializer)
      {
	error_code = def.row_initializer (class_mop);
	if (error_code != NO_ERROR)
	  {
	    assert (false);
	    return error_code;
	  }
      }

    return error_code;
  }

  int
  system_metadata_builder::build_vclass (const MOP class_mop, const system_metadata_definition &def)
  {
    int error_code = NO_ERROR;

    if (class_mop == nullptr)
      {
	// should not happen
	assert (false);
	return ER_FAILED;
      }

    SM_TEMPLATE *tmpl = smt_edit_class_mop (class_mop, AU_ALTER);
    if (tmpl == nullptr)
      {
	ASSERT_ERROR_AND_SET (error_code);
	return error_code;
      }

    /* the builder also redefines an existing vclass in place, so start empty */
    smt_reset_vclass_definition (tmpl);

    const std::vector <attribute> &attributes = def.attributes;
    for (const auto &attr: attributes)
      {
	const char *name = attr.name.data ();
	const char *type = attr.type.data ();

	switch (attr.kind)
	  {
	  case attribute_kind::COLUMN:
	    error_code = smt_add_attribute (tmpl, name, type, NULL);
	    break;
	  case attribute_kind::QUERY_SPEC:
	    error_code = smt_add_query_spec (tmpl, name);
	    break;
	  case attribute_kind::CLASS_METHOD:
	    /*
	     * Temporary: class method support in system view class for
	     * compatibility. To be removed when class/instance method support
	     * is officially dropped.
	     */
	    error_code = smt_add_class_method (tmpl, name, type);
	    break;
	  default:
	    error_code = ER_FAILED;
	    break;
	  }

	if (error_code != NO_ERROR)
	  {
	    assert (false);
	    smt_quit (tmpl);
	    return error_code;
	  }
      }

    error_code = sm_update_class (tmpl, NULL);
    if (error_code != NO_ERROR)
      {
	assert (false);
	smt_quit (tmpl);
	return error_code;
      }

    const authorization &auth = def.auth;
    if (auth.owner == nullptr)
      {
	assert (false);
	return ER_FAILED;
      }

    error_code = au_change_class_owner_including_partitions (class_mop, auth.owner);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    for (const grant &g : auth.grants)
      {
	assert (g.target_user != nullptr);
	error_code = au_grant (DB_OBJECT_CLASS, g.target_user, class_mop, g.auth, g.with_grant_option);
	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }
      }

    return error_code;
  }

  int
  system_metadata_builder::rebuild_vclasses (const std::vector<system_metadata_definition> &defs,
      const std::string &owner_prefix)
  {
    int error_code = NO_ERROR;

    for (const system_metadata_definition &def : defs)
      {
	/* find the existing vclass so that build_vclass () redefines it in place and its OID stays the same */
	MOP class_mop = db_find_class ((owner_prefix + def.name).c_str ());
	if (class_mop == nullptr)
	  {
	    ASSERT_ERROR_AND_SET (error_code);
	    return error_code;
	  }

	error_code = build_vclass (class_mop, def);
	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }

	/* a vclass has no instances, so its old representations are no longer needed */
	error_code = sm_destroy_representations (class_mop);
	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }
      }

    return NO_ERROR;
  }
}
