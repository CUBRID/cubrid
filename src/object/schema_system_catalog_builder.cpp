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

#include "schema_system_catalog_builder.hpp"

// #include "dbi.h" /* db_create_class () */
#include "db.h"
#include "dbtype.h"
// #include "dbtype_function.h" /* DB_IS_NULL () */
#include "authenticate.h"
#include "class_object.h" /* SM_TEMPLATE */
#include "schema_template.h" /* smt_edit_class_mop() */
#include "schema_manager.h"
#include "locator_cl.h"
#include "schema_system_catalog_definition.hpp"

namespace cubschema
{
  MOP
  system_catalog_builder::create_and_mark_system_class (std::string_view name)
  {
    if (name.empty ())
      {
	// should not happen
	assert (false);
	return nullptr;
      }

    // class_mop
    MOP class_mop = db_create_class (name.data ());
    if (class_mop == nullptr)
      {
	assert (er_errid () != NO_ERROR);
	return nullptr;
      }
    sm_mark_system_class (class_mop, 1);

    return class_mop;
  }

  int
  system_catalog_builder::build_class (const MOP class_mop, const system_catalog_definition &catalog_def)
  {
    int error_code = NO_ERROR;

    if (class_mop == nullptr)
      {
	// should not happen
	assert (false);
	return ER_FAILED;
      }

    // define
    SM_TEMPLATE *def = smt_edit_class_mop (class_mop, AU_ALTER);

    const std::vector <column> &attributes = catalog_def.attributes;
    for (const auto &attr: attributes)
      {
	const char *name = attr.name.data ();
	const char *type = attr.type.data ();
	error_code = smt_add_attribute (def, name, type, NULL);
	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }

	if (!DB_IS_NULL (&attr.default_value))
	  {
	    error_code = smt_set_attribute_default (def, name, 0, (DB_VALUE *) &attr.default_value, NULL);
	    if (error_code != NO_ERROR)
	      {
		return error_code;
	      }
	  }
      }

    error_code = sm_update_class (def, NULL);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    const std::vector <constraint> &constraints = catalog_def.constraints;
    for (const auto &c: constraints)
      {
	DB_CONSTRAINT_TYPE type = c.type;

	const char *name = c.name.data();
	int is_class_attribute = c.is_class_attributes ? 1 : 0;
	if (type == DB_CONSTRAINT_INDEX)
	  {
	    error_code = db_add_constraint (class_mop, type, name, (const char **) c.attribute_names.data(),
					    is_class_attribute);
	  }
	else if (type == DB_CONSTRAINT_NOT_NULL)
	  {
	    error_code = db_constrain_non_null (class_mop, name, is_class_attribute, 1);
	  }
	else
	  {
	    // TODO: error handling, there is no such a cases in the legacy code
	    assert (false);
	    error_code = ER_GENERIC_ERROR;
	  }

	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }
      }

    if (locator_has_heap (class_mop) == NULL)
      {
	assert (er_errid () != NO_ERROR);
	return er_errid ();
      }

    const authorization &auth = catalog_def.auth;
    if (auth.owner != nullptr)
      {
	error_code = au_change_owner (class_mop, auth.owner);
	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }
      }

    for (const grant &g : auth.grants)
      {
	error_code = au_grant (g.target_user, class_mop, AU_SELECT, false);
	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }
      }

    return error_code;
  }
}
