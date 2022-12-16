/*
 * Copyright 2008 Search Solution Corporation
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

#include "system_parameter.h"

SYSPRM_PARAM prm_Def[PRM_LAST_ID];

bool prm_Def_bool_val_array[PRM_LAST_ID];
int prm_Def_integer_val_array[PRM_LAST_ID];

// NOTE: this is implemented here as it seems it is not called
// however, should it ever be needed, it must be refactored as:
//  - either an indirection to another test specific implementation in the
//    test source
//  - or taken out of here and put only in whichever tests needs it
void *
prm_get_value (PARAM_ID prm_id)
{
  if (prm_Def[prm_id].value != nullptr)
    {
      return prm_Def[prm_id].value;
    }
  assert (false);
  return nullptr;
}

#if defined (WINDOWS)
// TODO: see system_parameter.h where these function are only inlined on non-WINDOWS
// and inlined elsewhere
bool
prm_get_bool_value (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (prm_Def[prm_id].value != nullptr);
  assert (prm_Def[prm_id].value == (void *)&prm_Def_bool_val_array[prm_id]);

  return PRM_GET_BOOL (prm_get_value (prm_id));
}

int
prm_get_integer_value (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (prm_Def[prm_id].value != nullptr);
  assert (prm_Def[prm_id].value == (void *)&prm_Def_integer_val_array[prm_id]);

  return PRM_GET_INT (prm_get_value (prm_id));
}
#endif


void
prm_set_bool_value (PARAM_ID prm_id, bool value)
{
  assert (prm_id <= PRM_LAST_ID);
  prm_Def[prm_id].datatype = PRM_BOOLEAN;

  // on demand, provide support for the value if not already set before
  // set value only once to avoid mistakes such as: different calls set
  // different value type to the same parameter
  if (prm_Def[prm_id].value == nullptr)
    {
      prm_Def[prm_id].value = (void *)&prm_Def_bool_val_array[prm_id];
    }

  PRM_GET_BOOL (prm_Def[prm_id].value) = value;
}

void
prm_set_integer_value (PARAM_ID prm_id, int value)
{
  assert (prm_id <= PRM_LAST_ID);
  prm_Def[prm_id].datatype = PRM_INTEGER;

  if (prm_Def[prm_id].value == nullptr)
    {
      prm_Def[prm_id].value = (void *)&prm_Def_integer_val_array[prm_id];
    }

  PRM_GET_INT (prm_Def[prm_id].value) = value;
}
