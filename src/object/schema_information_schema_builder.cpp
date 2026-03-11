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

#include "schema_information_schema_builder.hpp"

#include "authenticate.h"
#include "dbi.h"
#include "schema_information_schema_definition.hpp"
#include "schema_manager.h"

namespace cubschema
{
  MOP
  information_schema_builder::create_and_mark_system_class (const std::string_view name)
  {
    assert (!name.empty ());

    MOP class_mop = db_create_vclass (name.data());
    if (class_mop == nullptr)
      {
	return nullptr;
      }

    sm_mark_system_class (class_mop, 1);

    return class_mop;
  }

  int
  information_schema_builder::build_vclass (const MOP class_mop, const information_schema_definition &def)
  {
    assert (class_mop != nullptr);

    int error_code = NO_ERROR;

    const std::vector <attribute> &attributes = def.attributes;
    for (const auto &attr: attributes)
      {
	const char *name = attr.name.data ();
	const char *type = attr.type.data ();

	switch (attr.kind)
	  {
	  case attribute_kind::COLUMN:
	    error_code = db_add_attribute (class_mop, name, type, NULL);
	    break;
	  case attribute_kind::QUERY_SPEC:
	    error_code = db_add_query_spec (class_mop, name);
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
}
