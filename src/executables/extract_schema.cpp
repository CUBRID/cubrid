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


/*
 * extract_schema.cpp -
 */

#include "extract_schema.hpp"
#include "dbi.h"


void extract_context::clear_schema_workspace (void)
{
  if (vclass_list_has_using_index != NULL)
    {
      db_objlist_free (vclass_list_has_using_index);
      vclass_list_has_using_index = NULL;
    }

  if (classes != NULL)
    {
      db_objlist_free (classes);
      classes = NULL;
    }
}
