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
 * schema_system_catalog.hpp - Declare Main APIs for system catalog modules
 */

#ifndef _SCHEMA_SYSTEM_CATALOG_HPP_
#define _SCHEMA_SYSTEM_CATALOG_HPP_

#include <string_view>

int catcls_install (void);
int catcls_rebuild_vclasses (void);

bool sm_check_system_class_by_name (const std::string_view class_name);

bool sm_is_system_class (const std::string_view name);
bool sm_is_system_vclass (const std::string_view name);

#if defined (CS_MODE) || defined (SA_MODE)
struct db_object;

int catcls_add_collations (struct db_object *class_mop);
#endif

#if defined (SA_MODE)
int sm_mark_system_classes (void);
#endif

#endif /* _SCHEMA_SYSTEM_CATALOG_HPP_ */
